#include "sib_radio.h"

#include <furi_hal_subghz.h> // FuriHalSubGhzPreset
#include <storage/storage.h> // EXT_PATH
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/protocols/base.h>

#include <string.h>

#define TAG "Sibyl"

/* Manufacture keys and rainbow tables. Optional - the classifier never needs a
 * key - but with them the KeeLoq, Nice Flor-S and CAME Atomo decoders emit a
 * far richer parcel, which sharpens the per-press fingerprint. */
#define SIB_KEYSTORE_PATH      EXT_PATH("subghz/assets/keeloq_mfcodes")
#define SIB_KEYSTORE_USER_PATH EXT_PATH("subghz/assets/keeloq_mfcodes_user")
#define SIB_RT_NICE_FLOR_S     EXT_PATH("subghz/assets/nice_flor_s")
#define SIB_RT_CAME_ATOMO      EXT_PATH("subghz/assets/came_atomo")
#define SIB_RT_ALUTECH_AT_4N   EXT_PATH("subghz/assets/alutech_at_4n")

#define SIB_HUNT_SETTLE_MS 2
#define SIB_HUNT_SAMPLES   8
#define SIB_HUNT_SAMPLE_US 500

/*
 * The bands worth looking at, low to high. Everything a garage, gate, car,
 * blind, alarm, tyre or weather transmitter in the wild is likely to sit on,
 * including the two off-centre channels (433.42 for Somfy RTS, 434.42 for a
 * chunk of the cheap import market) that a 433.92-only scan walks straight
 * past. SIB_BAND_DEFAULT must stay pointing at 433.92.
 */
const SibBand sib_bands[SIB_BAND_COUNT] = {
    {.frequency = 300000000, .label = "300.00"},
    {.frequency = 303875000, .label = "303.87"},
    {.frequency = 310000000, .label = "310.00"},
    {.frequency = 315000000, .label = "315.00"},
    {.frequency = 318000000, .label = "318.00"},
    {.frequency = 330000000, .label = "330.00"},
    {.frequency = 345000000, .label = "345.00"},
    {.frequency = 390000000, .label = "390.00"},
    {.frequency = 418000000, .label = "418.00"},
    {.frequency = 433420000, .label = "433.42"},
    {.frequency = 433920000, .label = "433.92"},
    {.frequency = 434420000, .label = "434.42"},
    {.frequency = 434775000, .label = "434.77"},
    {.frequency = 868350000, .label = "868.35"},
    {.frequency = 868950000, .label = "868.95"},
    {.frequency = 915000000, .label = "915.00"},
};

const SibModPreset sib_mods[SIB_MOD_COUNT] = {
    {.label = "AM650", .preset = FuriHalSubGhzPresetOok650Async, .mod = SibModOok},
    {.label = "AM270", .preset = FuriHalSubGhzPresetOok270Async, .mod = SibModOok},
    {.label = "FM238", .preset = FuriHalSubGhzPreset2FSKDev238Async, .mod = SibModFsk},
    {.label = "FM476", .preset = FuriHalSubGhzPreset2FSKDev476Async, .mod = SibModFsk},
};

struct SibRadio {
    ViewDispatcher* view_dispatcher;
    uint32_t burst_event;

    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;
    const SubGhzDevice* device;

    FuriMutex* mutex; /* guards the session                */
    FuriMutex* dev_mutex; /* serialises SPI to the CC1101      */
    volatile bool running;

    uint32_t frequency;
    uint8_t preset;
    SibMod mod;

    /* the burst being assembled, worker thread only */
    SibPulseTrain cur;
    uint32_t cur_total_us;
    uint32_t cur_high_us;
    uint32_t pending_gap_us;

    /* Owned by the worker thread, which cannot afford it on its own stack. */
    SibScratch scratch;

    SibSession session;
    uint32_t last_burst_tick;
    volatile uint32_t rejected;

    volatile uint32_t edges;

    /* find band */
    FuriThread* hunt_thread;
    volatile bool hunt_running;
    SibHuntBand hunt[SIB_BAND_COUNT];
    uint32_t hunt_sweeps;
};

/* 64-bit FNV-1a over a decoded parcel string. */
static uint64_t sib_fnv64(const char* s) {
    uint64_t h = 1469598103934665603ULL;
    while(*s) {
        h ^= (uint8_t)(*s++);
        h *= 1099511628211ULL;
    }
    return h;
}

static int8_t sib_dbm_clamp(float rssi) {
    if(rssi > 0.0f) return 0;
    if(rssi < -127.0f) return -127;
    return (int8_t)rssi;
}

static float sib_read_rssi(SibRadio* radio) {
    float rssi = -127.0f;
    furi_mutex_acquire(radio->dev_mutex, FuriWaitForever);
    if(radio->device) rssi = subghz_devices_get_rssi(radio->device);
    furi_mutex_release(radio->dev_mutex);
    return rssi;
}

/* ------------------------------------------------- burst segmentation ---- */

static void sib_cur_reset(SibRadio* radio) {
    memset(&radio->cur, 0, sizeof(radio->cur));
    radio->cur_total_us = 0;
    radio->cur_high_us = 0;
}

/*
 * A burst has ended. Decide whether it was a packet, and if so fold it into
 * the session.
 *
 * The acceptance test is deliberately the app's own measurement rather than a
 * separate squelch: a run of pulses is a packet when a single symbol quantum
 * explains their widths. Random receiver noise has no such quantum and scores
 * far below the threshold, so nothing extra has to be tuned per band or per
 * gain setting - the same number works in a quiet field and next to a router.
 */
static void sib_close_burst(SibRadio* radio) {
    if(radio->cur.n < SIB_MIN_BURST_PULSES) {
        if(radio->cur.n > 0) radio->rejected++;
        sib_cur_reset(radio);
        return;
    }

    uint16_t te = sib_estimate_te_scratch(radio->cur.dur, radio->cur.n, &radio->scratch);
    uint16_t fit = te ? sib_measure_fit(radio->cur.dur, radio->cur.n, te, NULL, NULL) : 0;
    if(te == 0 || fit < SIB_MIN_BURST_FIT) {
        radio->rejected++;
        sib_cur_reset(radio);
        return;
    }

    furi_mutex_acquire(radio->mutex, FuriWaitForever);

    if(radio->session.n_bursts < SIB_MAX_BURSTS) {
        SibBurstSummary* s = &radio->session.burst[radio->session.n_bursts];
        s->n_pulses = radio->cur.n;
        s->total_us = radio->cur_total_us;
        s->high_us = radio->cur_high_us;
        s->gap_before_us = radio->pending_gap_us;

        /* The first accepted burst is the reference: it is the one kept in
         * full, measured for timing, and drawn as a trace. */
        if(radio->session.n_bursts == 0) {
            memcpy(&radio->session.train, &radio->cur, sizeof(SibPulseTrain));
            radio->session.frequency = radio->frequency;
            radio->session.preset = radio->preset;
        }
        radio->session.n_bursts++;
    }

    radio->last_burst_tick = furi_get_tick();
    furi_mutex_release(radio->mutex);

    if(radio->view_dispatcher) {
        view_dispatcher_send_custom_event(radio->view_dispatcher, radio->burst_event);
    }

    sib_cur_reset(radio);
}

/*
 * Worker pair callback: one level and how long it lasted, in the subghz
 * worker's thread. Everything raw that Sibyl knows comes through here.
 */
static void sib_on_pair(void* ctx, bool level, uint32_t duration) {
    SibRadio* radio = ctx;
    radio->edges++;

    if(radio->running) {
        if(!level && duration >= SIB_BURST_GAP_US) {
            /* Silence long enough to be the space between packets. */
            sib_close_burst(radio);
            radio->pending_gap_us = duration;
        } else {
            uint32_t d = duration;
            if(d > SIB_PULSE_MAX_US) d = SIB_PULSE_MAX_US;

            if(radio->cur.n == 0) radio->cur.first_level = level;
            if(radio->cur.n < SIB_MAX_PULSES) {
                radio->cur.dur[radio->cur.n++] = (uint16_t)d;
                radio->cur_total_us += d;
                if(level) radio->cur_high_us += d;
            } else {
                /* Out of room. Close what we have and flag it rather than
                 * silently measuring half a packet as if it were all of it. */
                radio->cur.truncated = true;
                sib_close_burst(radio);
            }
        }
    }

    subghz_receiver_decode(radio->receiver, level, duration);
}

static void sib_on_overrun(void* ctx) {
    SibRadio* radio = ctx;
    sib_cur_reset(radio);
    subghz_receiver_reset(radio->receiver);
}

/* Fired by the receiver on every successful decode, in the worker thread. */
static void sib_on_decode(SubGhzReceiver* receiver, SubGhzProtocolDecoderBase* decoder, void* ctx) {
    SibRadio* radio = ctx;
    const SubGhzProtocol* proto = decoder->protocol;
    if(!proto) {
        subghz_receiver_reset(receiver);
        return;
    }

    FuriString* dump = furi_string_alloc();
    uint64_t fp = 0;
    if(subghz_protocol_decoder_base_get_string(decoder, dump) && furi_string_size(dump)) {
        fp = sib_fnv64(furi_string_get_cstr(dump));
    } else {
        fp = ((uint64_t)subghz_protocol_decoder_base_get_hash_data(decoder) << 8);
    }
    furi_string_free(dump);

    int8_t rssi = sib_dbm_clamp(sib_read_rssi(radio));

    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    radio->session.decoded = true;
    strncpy(radio->session.protocol, proto->name ? proto->name : "?",
            sizeof(radio->session.protocol) - 1);
    radio->session.protocol[sizeof(radio->session.protocol) - 1] = '\0';
    radio->session.proto_static = (proto->type == SubGhzProtocolTypeStatic);
    radio->session.proto_dynamic = (proto->type == SubGhzProtocolTypeDynamic);
    radio->session.proto_fp = fp;
    if(rssi > radio->session.rssi) radio->session.rssi = rssi;

    /* A decode is a transmission even if the raw segmenter never accepted a
     * burst - a packet the firmware can read is not noise by definition. */
    radio->last_burst_tick = furi_get_tick();
    furi_mutex_release(radio->mutex);

    if(radio->view_dispatcher) {
        view_dispatcher_send_custom_event(radio->view_dispatcher, radio->burst_event);
    }

    subghz_receiver_reset(receiver);
}

/* ------------------------------------------------------ device bring-up -- */

static void sib_device_up(SibRadio* radio, uint32_t frequency, uint8_t preset) {
    furi_hal_power_suppress_charge_enter(); /* the charger is a noise source */

    subghz_devices_init();
    radio->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    subghz_devices_begin(radio->device);
    subghz_devices_reset(radio->device);
    subghz_devices_load_preset(radio->device, preset, NULL);

    if(!subghz_devices_is_frequency_valid(radio->device, frequency)) {
        frequency = sib_bands[SIB_BAND_DEFAULT].frequency;
    }
    subghz_devices_set_frequency(radio->device, frequency);
}

static void sib_device_down(SibRadio* radio) {
    furi_mutex_acquire(radio->dev_mutex, FuriWaitForever);
    subghz_devices_idle(radio->device);
    subghz_devices_sleep(radio->device);
    subghz_devices_end(radio->device);
    subghz_devices_deinit();
    radio->device = NULL;
    furi_mutex_release(radio->dev_mutex);

    furi_hal_power_suppress_charge_exit();
}

/* ------------------------------------------------------------ lifecycle -- */

SibRadio* sib_radio_alloc(ViewDispatcher* view_dispatcher, uint32_t burst_event) {
    SibRadio* radio = malloc(sizeof(SibRadio));
    memset(radio, 0, sizeof(SibRadio));

    radio->view_dispatcher = view_dispatcher;
    radio->burst_event = burst_event;
    radio->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    radio->dev_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    radio->frequency = sib_bands[SIB_BAND_DEFAULT].frequency;
    radio->preset = FuriHalSubGhzPresetOok650Async;
    radio->mod = SibModOok;
    radio->session.rssi = -127;

    radio->environment = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(radio->environment, (void*)&subghz_protocol_registry);
    subghz_environment_set_nice_flor_s_rainbow_table_file_name(radio->environment, SIB_RT_NICE_FLOR_S);
    subghz_environment_set_came_atomo_rainbow_table_file_name(radio->environment, SIB_RT_CAME_ATOMO);
    subghz_environment_set_alutech_at_4n_rainbow_table_file_name(radio->environment, SIB_RT_ALUTECH_AT_4N);
    if(!subghz_environment_load_keystore(radio->environment, SIB_KEYSTORE_PATH)) {
        FURI_LOG_W(TAG, "no keystore, decoding anyway");
    }
    subghz_environment_load_keystore(radio->environment, SIB_KEYSTORE_USER_PATH);

    radio->receiver = subghz_receiver_alloc_init(radio->environment);
    subghz_receiver_set_filter(radio->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(radio->receiver, sib_on_decode, radio);

    radio->worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(radio->worker, sib_on_overrun);
    subghz_worker_set_pair_callback(radio->worker, sib_on_pair);
    subghz_worker_set_context(radio->worker, radio);

    return radio;
}

void sib_radio_free(SibRadio* radio) {
    furi_assert(radio);
    sib_radio_hunt_stop(radio);
    sib_radio_stop(radio);
    subghz_receiver_free(radio->receiver);
    subghz_environment_free(radio->environment);
    subghz_worker_free(radio->worker);
    furi_mutex_free(radio->dev_mutex);
    furi_mutex_free(radio->mutex);
    free(radio);
}

void sib_radio_configure(SibRadio* radio, uint32_t frequency, uint8_t preset, SibMod mod) {
    furi_assert(radio);
    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    radio->frequency = frequency;
    radio->preset = preset;
    radio->mod = mod;
    furi_mutex_release(radio->mutex);
}

void sib_radio_reset_session(SibRadio* radio) {
    furi_assert(radio);
    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    memset(&radio->session, 0, sizeof(radio->session));
    radio->session.rssi = -127;
    radio->last_burst_tick = 0;
    furi_mutex_release(radio->mutex);

    sib_cur_reset(radio);
    radio->pending_gap_us = 0;
    radio->rejected = 0;
}

void sib_radio_start(SibRadio* radio) {
    furi_assert(radio);
    if(radio->running || radio->hunt_running) return;

    sib_radio_reset_session(radio);
    radio->edges = 0;

    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    uint32_t freq = radio->frequency;
    uint8_t preset = radio->preset;
    furi_mutex_release(radio->mutex);

    sib_device_up(radio, freq, preset);

    subghz_receiver_reset(radio->receiver);
    subghz_worker_start(radio->worker);
    subghz_devices_start_async_rx(radio->device, (void*)subghz_worker_rx_callback, radio->worker);

    radio->running = true;
}

void sib_radio_stop(SibRadio* radio) {
    furi_assert(radio);
    if(!radio->running) return;
    radio->running = false;

    /* The worker can be inside a decode - and so inside an RSSI read - right
     * now, so tearing RX down takes the same lock it does. */
    furi_mutex_acquire(radio->dev_mutex, FuriWaitForever);
    subghz_devices_stop_async_rx(radio->device);
    furi_mutex_release(radio->dev_mutex);

    subghz_worker_stop(radio->worker);
    sib_device_down(radio);
}

bool sib_radio_is_running(SibRadio* radio) {
    furi_assert(radio);
    return radio->running;
}

uint8_t sib_radio_burst_count(SibRadio* radio) {
    furi_assert(radio);
    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    uint8_t n = radio->session.n_bursts;
    furi_mutex_release(radio->mutex);
    return n;
}

uint32_t sib_radio_idle_ms(SibRadio* radio) {
    furi_assert(radio);
    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    uint32_t last = radio->last_burst_tick;
    furi_mutex_release(radio->mutex);
    if(last == 0) return UINT32_MAX;
    return furi_get_tick() - last;
}

bool sib_radio_session_ready(SibRadio* radio) {
    furi_assert(radio);
    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    bool have = radio->session.n_bursts > 0 || radio->session.decoded;
    uint32_t last = radio->last_burst_tick;
    furi_mutex_release(radio->mutex);

    if(!have || last == 0) return false;
    return (furi_get_tick() - last) >= SIB_SESSION_IDLE_MS;
}

bool sib_radio_snapshot(SibRadio* radio, SibSession* out) {
    furi_assert(radio);
    if(!out) return false;

    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    bool have = radio->session.n_bursts > 0 || radio->session.decoded;
    if(have) {
        memcpy(out, &radio->session, sizeof(SibSession));
        /* A decode with no accepted raw burst still needs the tune recorded,
         * because band and modulation are evidence in their own right. */
        if(out->frequency == 0) {
            out->frequency = radio->frequency;
            out->preset = radio->preset;
        }
    }
    furi_mutex_release(radio->mutex);

    return have;
}

/* ----------------------------------------------------------- diagnostics -- */

float sib_radio_rssi(SibRadio* radio) {
    furi_assert(radio);
    if(!radio->running && !radio->hunt_running) return -127.0f;
    return sib_read_rssi(radio);
}

uint32_t sib_radio_edges(SibRadio* radio) {
    furi_assert(radio);
    return radio->edges;
}

uint32_t sib_radio_rejected(SibRadio* radio) {
    furi_assert(radio);
    return radio->rejected;
}

bool sib_radio_decoded(SibRadio* radio) {
    furi_assert(radio);
    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    bool d = radio->session.decoded;
    furi_mutex_release(radio->mutex);
    return d;
}

/* ------------------------------------------------------------ find band -- */

static int32_t sib_hunt_thread(void* ctx) {
    SibRadio* radio = ctx;

    while(radio->hunt_running) {
        for(uint8_t i = 0; i < SIB_BAND_COUNT && radio->hunt_running; i++) {
            furi_mutex_acquire(radio->dev_mutex, FuriWaitForever);
            if(!radio->device) {
                furi_mutex_release(radio->dev_mutex);
                break;
            }
            subghz_devices_idle(radio->device);
            subghz_devices_set_frequency(radio->device, sib_bands[i].frequency);
            subghz_devices_set_rx(radio->device);
            furi_mutex_release(radio->dev_mutex);

            furi_delay_ms(SIB_HUNT_SETTLE_MS);

            /* Peak-hold across the dwell: a transmitter keying its carrier on
             * and off spends part of the window silent, so the max is what
             * matters, not the mean. */
            float peak = -127.0f;
            for(uint8_t s = 0; s < SIB_HUNT_SAMPLES; s++) {
                float r = sib_read_rssi(radio);
                if(r > peak) peak = r;
                furi_delay_us(SIB_HUNT_SAMPLE_US);
            }

            int8_t dbm = sib_dbm_clamp(peak);

            furi_mutex_acquire(radio->mutex, FuriWaitForever);
            SibHuntBand* b = &radio->hunt[i];
            if(!b->seen) {
                b->seen = true;
                b->floor_dbm = dbm;
                b->peak_dbm = dbm;
            } else {
                if(dbm > b->peak_dbm) b->peak_dbm = dbm;
                if(dbm < b->floor_dbm) b->floor_dbm = dbm;
            }
            b->last_dbm = dbm;
            furi_mutex_release(radio->mutex);
        }

        furi_mutex_acquire(radio->mutex, FuriWaitForever);
        radio->hunt_sweeps++;
        furi_mutex_release(radio->mutex);
    }

    return 0;
}

void sib_radio_hunt_start(SibRadio* radio) {
    furi_assert(radio);
    if(radio->hunt_running || radio->running) return;

    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    memset(radio->hunt, 0, sizeof(radio->hunt));
    radio->hunt_sweeps = 0;
    furi_mutex_release(radio->mutex);

    /* Widest OOK filter for the sweep: we are measuring raw carrier power, not
     * demodulating, so a wide window catches transmitters whose exact centre
     * is off. */
    sib_device_up(radio, sib_bands[SIB_BAND_DEFAULT].frequency, FuriHalSubGhzPresetOok650Async);
    subghz_devices_set_rx(radio->device);

    radio->hunt_running = true;
    radio->hunt_thread = furi_thread_alloc_ex("SibHunt", 1024, sib_hunt_thread, radio);
    furi_thread_start(radio->hunt_thread);
}

void sib_radio_hunt_stop(SibRadio* radio) {
    furi_assert(radio);
    if(!radio->hunt_running) return;
    radio->hunt_running = false;

    furi_thread_join(radio->hunt_thread);
    furi_thread_free(radio->hunt_thread);
    radio->hunt_thread = NULL;

    sib_device_down(radio);
}

bool sib_radio_hunt_is_running(SibRadio* radio) {
    furi_assert(radio);
    return radio->hunt_running;
}

uint8_t sib_radio_hunt_snapshot(SibRadio* radio, SibHuntBand* out, uint8_t max) {
    furi_assert(radio);
    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    uint8_t n = SIB_BAND_COUNT < max ? SIB_BAND_COUNT : max;
    for(uint8_t i = 0; i < n; i++) out[i] = radio->hunt[i];
    furi_mutex_release(radio->mutex);
    return n;
}

uint32_t sib_radio_hunt_sweeps(SibRadio* radio) {
    furi_assert(radio);
    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    uint32_t n = radio->hunt_sweeps;
    furi_mutex_release(radio->mutex);
    return n;
}

int8_t sib_radio_hunt_best(SibRadio* radio) {
    furi_assert(radio);
    int8_t best = -1;
    int16_t best_delta = SIB_HUNT_MIN_DELTA_DB - 1;

    furi_mutex_acquire(radio->mutex, FuriWaitForever);
    for(uint8_t i = 0; i < SIB_BAND_COUNT; i++) {
        const SibHuntBand* b = &radio->hunt[i];
        if(!b->seen) continue;
        int16_t delta = (int16_t)b->peak_dbm - (int16_t)b->floor_dbm;
        if(delta > best_delta) {
            best_delta = delta;
            best = (int8_t)i;
        }
    }
    furi_mutex_release(radio->mutex);

    return best;
}
