#include "sib_library.h"

#include <string.h>

/* --------------------------------------------------------- class table --- */

typedef struct {
    const char* name;
    const char* tagline;
    SibClassEntry entry;
} SibClassRow;

static const SibClassRow sib_classes[SibClassCount] = {
    [SibClassUnknown] =
        {
            .name = "Unidentified",
            .tagline = "Something transmitted, but not enough to name it",
            .entry =
                {
                    .what = "A packet was heard on this band, but neither the "
                            "decoder nor the timing fingerprint matched anything "
                            "Sibyl recognises.",
                    .how = "The signal may use a proprietary line code, a "
                           "modulation this preset cannot demodulate, or it may "
                           "simply have been too weak to measure cleanly.",
                    .security = "Nothing can be said about the security of a device "
                                "that has not been identified. An unknown signal is "
                                "not a safe signal and is not a dangerous one.",
                    .limits = "Try moving closer, switching modulation between AM "
                              "and FM, or running Find Band to check the "
                              "transmitter is really on this frequency.",
                },
        },
    [SibClassGateRemote] =
        {
            .name = "Gate remote",
            .tagline = "Garage door, gate or barrier handset",
            .entry =
                {
                    .what = "The handset that opens a garage door, driveway gate or "
                            "car park barrier. Usually a two to four button fob on a "
                            "keyring or clipped to a sun visor.",
                    .how = "A short burst of on-off keyed data, repeated several "
                           "times in a row so that one lost copy does not cost you "
                           "the door. The symbol width is generous - a third of a "
                           "millisecond is typical - because the receiver is cheap.",
                    .security = "Older installations send the same code every time, "
                                "so a recording of one press opens the door forever. "
                                "Newer ones roll the code on each press, which "
                                "defeats a plain replay.",
                    .limits = "Sibyl cannot tell a gate from a garage door: they are "
                              "the same radio link driving different motors. Whether "
                              "this particular remote rolls its code is answered by "
                              "pressing it twice - see the Repeat check.",
                },
        },
    [SibClassCarFob] =
        {
            .name = "Car key fob",
            .tagline = "Vehicle remote locking or car alarm handset",
            .entry =
                {
                    .what = "The lock, unlock and boot-release remote for a car, or "
                            "the handset of an aftermarket car alarm. Not the same "
                            "thing as the immobiliser, which is a separate short "
                            "range link inside the ignition.",
                    .how = "A longer packet than a gate remote, carrying a serial "
                           "number, a button code and a counter that advances on "
                           "every press. Many manufacturers use frequency shift "
                           "keying rather than on-off keying for better range.",
                    .security = "Every car fob sold this century rolls its code, so "
                                "replaying a recording does nothing. The practical "
                                "attack is a relay: two radios that stretch the link "
                                "between key and car across a driveway.",
                    .limits = "Sibyl only hears the fob. It cannot tell you which car "
                              "answered, and it does not decode or store the code. "
                              "For relay attacks specifically, use Cardea.",
                },
        },
    [SibClassTpms] =
        {
            .name = "Tyre sensor",
            .tagline = "TPMS pressure sensor inside a wheel",
            .entry =
                {
                    .what = "A battery powered sensor bolted to the inside of a wheel "
                            "rim, next to the valve. It reports tyre pressure and "
                            "temperature to the car so the dashboard warning light "
                            "knows when to come on.",
                    .how = "A very short, fast packet - a handful of milliseconds at "
                           "tens of microseconds per symbol, usually Manchester coded "
                           "and frequency shift keyed. It transmits every minute or "
                           "so while moving and goes almost silent when parked.",
                    .security = "The packet is not encrypted and contains a fixed "
                                "sensor ID. That ID is effectively a licence plate "
                                "you cannot cover: anyone with a receiver by the road "
                                "can log when your car passes.",
                    .limits = "Sibyl identifies the class of signal, not the vehicle. "
                              "For the tracking angle in detail, including which "
                              "sensor families leak what, see Odograph.",
                },
        },
    [SibClassWeather] =
        {
            .name = "Weather sensor",
            .tagline = "Outdoor temperature, rain or wind transmitter",
            .entry =
                {
                    .what = "The outdoor half of a weather station: a temperature and "
                            "humidity probe, a rain gauge or an anemometer, sending "
                            "readings to the display unit indoors.",
                    .how = "A slow on-off keyed packet of a few dozen bits carrying a "
                           "sensor ID, a channel number and the reading, sent on a "
                           "fixed schedule of roughly every thirty to sixty seconds "
                           "whether anything changed or not.",
                    .security = "Unencrypted and unauthenticated by design. Anyone "
                                "nearby can read the values, and anyone can transmit "
                                "fake ones the display will believe. The stakes are "
                                "usually low, but a frost alarm is worth thinking "
                                "about.",
                    .limits = "The regular schedule is the giveaway. If you caught "
                              "this signal once, wait a minute on the same frequency "
                              "and it should come back on its own.",
                },
        },
    [SibClassDoorbell] =
        {
            .name = "Doorbell",
            .tagline = "Wireless doorbell push button",
            .entry =
                {
                    .what = "The push button beside a front door, talking to a chime "
                            "unit plugged in somewhere inside the house.",
                    .how = "A short fixed code hammered out many times in a row - far "
                           "more repeats than a gate remote uses - because the button "
                           "gets one chance to be heard and its battery only has to "
                           "last for a fraction of a second.",
                    .security = "Almost always a fixed code with no authentication, "
                                "so it can be recorded and replayed. The consequence "
                                "is a chime ringing, which is a nuisance rather than "
                                "a break-in, but the same chip often runs the door "
                                "sensors too.",
                    .limits = "A doorbell and a mains socket remote can use the same "
                              "encoder chip and the same code length. The high repeat "
                              "count is what separates them, and it is a tendency "
                              "rather than a rule.",
                },
        },
    [SibClassSocket] =
        {
            .name = "Remote socket",
            .tagline = "Plug-in mains socket or RF light switch",
            .entry =
                {
                    .what = "A mains socket adapter or in-wall switch controlled from "
                            "a little RF handset - the cheap way to make a lamp or a "
                            "Christmas tree switchable from the sofa.",
                    .how = "A twenty-four bit fixed code from an encoder chip: twenty "
                           "bits of address, four of channel and command. The address "
                           "is set at the factory or, on older units, by DIP switches "
                           "inside the case.",
                    .security = "Trivially replayable, and where the address is set "
                                "by DIP switches the entire keyspace is a few "
                                "thousand combinations. Fine for a lamp, not fine for "
                                "anything that matters.",
                    .limits = "This is the most crowded corner of the 433 MHz band. "
                              "Sockets, doorbells, garden lights and alarm sensors "
                              "all ship the same encoder, so treat the ranking below "
                              "as a shortlist rather than an answer.",
                },
        },
    [SibClassSensor] =
        {
            .name = "Alarm sensor",
            .tagline = "PIR, door contact or smoke detector",
            .entry =
                {
                    .what = "A battery powered sensor reporting to an alarm panel: a "
                            "motion detector, a magnetic door or window contact, or a "
                            "smoke or water detector.",
                    .how = "Silent until something happens, then a short packet "
                            "carrying the sensor ID and an event code, sent once or a "
                            "few times. Most also send a slow supervision heartbeat "
                            "so the panel knows they are still alive.",
                    .security = "Fixed-code sensors can be replayed, but the more "
                                "serious problem is the opposite direction: the link "
                                "is one-way and unauthenticated, so a panel that "
                                "loses contact with a sensor may simply not notice.",
                    .limits = "Sibyl cannot tell a PIR from a door contact from the "
                              "radio alone - they use the same transmitter and differ "
                              "only in the event byte, which is inside the payload.",
                },
        },
    [SibClassBlinds] =
        {
            .name = "Blind motor",
            .tagline = "Roller blind, shutter or awning remote",
            .entry =
                {
                    .what = "The handset for a motorised roller blind, roller shutter, "
                            "awning or projection screen. Often a slim wall-mounted "
                            "remote with up, stop and down.",
                    .how = "A distinctly slow symbol rate compared to a gate remote, "
                           "with a long wake-up preamble to bring the motor's "
                           "receiver out of sleep before the payload starts. Several "
                           "families also use a frequency slightly off the usual "
                           "433.92 centre.",
                    .security = "The mainstream systems roll their codes, so a replay "
                                "does not work. The exposure is physical: a shutter "
                                "that can be opened from the pavement is a window "
                                "that can be opened from the pavement.",
                    .limits = "If your remote is on 433.42 rather than 433.92, Sibyl "
                              "will hear it best on the matching band - run Find Band "
                              "if the capture looks weak or ragged.",
                },
        },
    [SibClassMeter] =
        {
            .name = "Meter / telemetry",
            .tagline = "Utility meter or industrial telemetry",
            .entry =
                {
                    .what = "A gas, water, heat or electricity meter reporting its "
                            "reading for drive-by or fixed-network collection, or a "
                            "similar industrial telemetry node.",
                    .how = "A long, fast frequency shift keyed frame in the 868 or "
                           "915 MHz band, sent on a schedule measured in minutes. "
                           "The frames are much larger than anything a handheld "
                           "remote sends.",
                    .security = "Modern deployments encrypt the payload, older ones "
                                "do not. Even encrypted, the frames are addressed, so "
                                "presence and transmission timing leak occupancy "
                                "patterns for a building.",
                    .limits = "Sibyl measures the shape of the frame only. It does "
                              "not decode meter payloads and cannot tell you which "
                              "utility, meter or premises a frame belongs to.",
                },
        },
    [SibClassIndustrial] =
        {
            .name = "Industrial remote",
            .tagline = "Crane, hoist or site machinery control",
            .entry =
                {
                    .what = "The pendant or belly-box that drives an overhead crane, "
                            "a hoist, a concrete pump or a tail lift - the chunky "
                            "yellow controller with a mushroom stop button.",
                    .how = "Unlike a door remote, which fires once per press, these "
                           "transmit continuously for as long as a control is held, "
                           "so the machine stops the moment the link drops. That "
                           "produces a long unbroken run of repeats.",
                    .security = "Safety-rated systems pair to one machine and use "
                                "rolling codes with a watchdog. Cheap imports do not, "
                                "and a fixed-code hoist controller is a genuinely "
                                "dangerous thing to have on site.",
                    .limits = "The continuous repeat pattern is the signature here. A "
                              "single short capture of one of these looks much like a "
                              "gate remote, so hold the control down while capturing.",
                },
        },
};

const char* sib_class_name(SibClass cls) {
    if(cls >= SibClassCount) return sib_classes[SibClassUnknown].name;
    return sib_classes[cls].name;
}

const char* sib_class_tagline(SibClass cls) {
    if(cls >= SibClassCount) return sib_classes[SibClassUnknown].tagline;
    return sib_classes[cls].tagline;
}

const SibClassEntry* sib_class_entry(SibClass cls) {
    if(cls >= SibClassCount) return &sib_classes[SibClassUnknown].entry;
    return &sib_classes[cls].entry;
}

/* ------------------------------------------------------ protocol table --- */

/*
 * Ordered most specific first: the first keyword that appears in the decoder's
 * name wins. "Nice One" has to be tested before "Nice", and the encoder chips
 * sit at the bottom so a branded protocol is never swallowed by a generic one.
 *
 * device_specific = true means the protocol belongs to one product family, so
 * a decode is an identification. device_specific = false means the protocol is
 * an encoder chip sold to anyone, so a decode narrows the field but names
 * nothing - bias_mask lists what it plausibly could be.
 */
typedef struct {
    const char* keyword;
    SibProtoInfo info;
} SibProtoRow;

#define GATE SIB_BIT(SibClassGateRemote)
#define CARF SIB_BIT(SibClassCarFob)
#define BELL SIB_BIT(SibClassDoorbell)
#define SOCK SIB_BIT(SibClassSocket)
#define SENS SIB_BIT(SibClassSensor)
#define BLND SIB_BIT(SibClassBlinds)
#define WTHR SIB_BIT(SibClassWeather)
#define INDU SIB_BIT(SibClassIndustrial)

static const SibProtoRow sib_protocols[] = {
    /* -------- blind, shutter and awning motors -------- */
    {"somfy", {SibClassBlinds, true, BLND, "Somfy RTS - blind/awning motor"}},
    {"dooya", {SibClassBlinds, true, BLND, "Dooya - roller blind motor"}},
    {"a-ok", {SibClassBlinds, true, BLND, "A-OK - roller blind motor"}},

    /* -------- garage, gate and barrier automation -------- */
    {"security+", {SibClassGateRemote, true, GATE, "Chamberlain/LiftMaster opener"}},
    {"chamb", {SibClassGateRemote, true, GATE, "Chamberlain garage opener"}},
    {"hormann", {SibClassGateRemote, true, GATE, "Hormann garage/gate opener"}},
    {"marantec", {SibClassGateRemote, true, GATE, "Marantec garage opener"}},
    {"nice one", {SibClassGateRemote, true, GATE, "Nice One - gate automation"}},
    {"nice", {SibClassGateRemote, true, GATE, "Nice - gate/garage automation"}},
    {"came twee", {SibClassGateRemote, true, GATE, "CAME TWEE - gate remote"}},
    {"came", {SibClassGateRemote, true, GATE, "CAME - gate automation"}},
    {"faac", {SibClassGateRemote, true, GATE, "FAAC - gate automation"}},
    {"beninca", {SibClassGateRemote, true, GATE, "Beninca - gate automation"}},
    {"prastel", {SibClassGateRemote, true, GATE, "Prastel - gate automation"}},
    {"aprimatic", {SibClassGateRemote, true, GATE, "Aprimatic - gate automation"}},
    {"alutech", {SibClassGateRemote, true, GATE, "Alutech - gate automation"}},
    {"kinggates", {SibClassGateRemote, true, GATE, "King Gates - gate automation"}},
    {"dickert", {SibClassGateRemote, true, GATE, "Dickert - gate/garage remote"}},
    {"sommer", {SibClassGateRemote, true, GATE, "Sommer - garage opener"}},
    {"novoferm", {SibClassGateRemote, true, GATE, "Novoferm - garage opener"}},
    {"genie", {SibClassGateRemote, true, GATE, "Genie - garage opener"}},
    {"megacode", {SibClassGateRemote, true, GATE, "Linear Megacode - gate/garage"}},
    {"multicode", {SibClassGateRemote, true, GATE, "Linear Multicode - gate/garage"}},
    {"linear", {SibClassGateRemote, true, GATE, "Linear - gate/garage remote"}},
    {"gatetx", {SibClassGateRemote, true, GATE, "GateTX - gate remote"}},
    {"gangqi", {SibClassGateRemote, true, GATE, "GangQi - barrier/gate remote"}},
    {"doitrand", {SibClassGateRemote, true, GATE, "Doitrand - gate remote"}},
    {"ansonic", {SibClassGateRemote, true, GATE, "Ansonic - gate remote"}},
    {"nero", {SibClassGateRemote, true, GATE | SENS, "Nero - gate/alarm remote"}},
    {"power smart", {SibClassGateRemote, true, GATE, "Power Smart - gate remote"}},
    {"bett", {SibClassGateRemote, true, GATE, "BETT - gate remote"}},

    /* -------- vehicles -------- */
    {"star line", {SibClassCarFob, true, CARF, "StarLine - car alarm handset"}},
    {"starline", {SibClassCarFob, true, CARF, "StarLine - car alarm handset"}},
    {"scher-khan", {SibClassCarFob, true, CARF, "Scher-Khan - car alarm handset"}},
    {"magicar", {SibClassCarFob, true, CARF, "Magicar - car alarm handset"}},
    {"pandora", {SibClassCarFob, true, CARF, "Pandora - car alarm handset"}},
    {"kia", {SibClassCarFob, true, CARF, "KIA - vehicle remote"}},
    {"tpms", {SibClassTpms, true, SIB_BIT(SibClassTpms), "TPMS - tyre pressure sensor"}},

    /* -------- alarm and household sensors -------- */
    {"magellan", {SibClassSensor, true, SENS, "Magellan - alarm sensor"}},
    {"hollarm", {SibClassSensor, true, SENS, "Hollarm - alarm sensor"}},
    {"honeywell", {SibClassDoorbell, true, BELL | SENS, "Honeywell - wireless doorbell"}},

    /* -------- weather -------- */
    {"acurite", {SibClassWeather, true, WTHR, "Acurite - weather sensor"}},
    {"lacrosse", {SibClassWeather, true, WTHR, "LaCrosse - weather sensor"}},
    {"oregon", {SibClassWeather, true, WTHR, "Oregon Scientific - weather sensor"}},
    {"nexus", {SibClassWeather, true, WTHR, "Nexus - weather sensor"}},

    /* -------- mains switching -------- */
    {"intertechno", {SibClassSocket, true, SOCK, "Intertechno - mains socket"}},

    /*
     * -------- encoder chips: a decode here names silicon, not a product -----
     * These are sold by the reel and fitted to anything that needs a cheap
     * one-way link. Naming one is a real result - it tells you the code is
     * fixed and therefore replayable - but it does not tell you what the code
     * opens, so cls stays Unknown and the fingerprint does the ranking.
     */
    {"keeloq", {SibClassUnknown, false, GATE | CARF | BLND, "KeeLoq rolling-code chip"}},
    {"princeton", {SibClassUnknown, false, GATE | BELL | SOCK | SENS | INDU,
                   "Princeton PT2262 encoder chip"}},
    {"ev1527", {SibClassUnknown, false, GATE | BELL | SOCK | SENS,
                "EV1527 encoder chip"}},
    {"smc5326", {SibClassUnknown, false, GATE | SOCK | INDU, "SMC5326 encoder chip"}},
    {"ht12", {SibClassUnknown, false, GATE | SOCK | SENS, "Holtek HT12 encoder chip"}},
    {"holtek", {SibClassUnknown, false, GATE | BELL | SOCK | SENS,
                "Holtek encoder chip"}},
    {"phoenix", {SibClassUnknown, false, GATE | SOCK, "Phoenix encoder"}},
};

#define SIB_PROTOCOL_COUNT (sizeof(sib_protocols) / sizeof(sib_protocols[0]))

/* Case-insensitive substring search. The decoder names are short and this runs
 * once per capture, so a plain scan is the right amount of machinery. */
static bool sib_contains_ci(const char* haystack, const char* needle) {
    if(!haystack || !needle || !*needle) return false;
    for(const char* h = haystack; *h; h++) {
        const char* a = h;
        const char* b = needle;
        while(*a && *b) {
            char ca = *a, cb = *b;
            if(ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if(cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            if(ca != cb) break;
            a++;
            b++;
        }
        if(!*b) return true;
    }
    return false;
}

uint16_t sib_protocol_count(void) {
    return (uint16_t)SIB_PROTOCOL_COUNT;
}

bool sib_protocol_at(uint16_t index, const char** keyword, SibProtoInfo* out) {
    if(index >= SIB_PROTOCOL_COUNT) return false;
    if(keyword) *keyword = sib_protocols[index].keyword;
    if(out) *out = sib_protocols[index].info;
    return true;
}

bool sib_protocol_lookup(const char* protocol, SibProtoInfo* out) {
    if(out) memset(out, 0, sizeof(*out));
    if(!protocol || !*protocol) return false;

    for(size_t i = 0; i < SIB_PROTOCOL_COUNT; i++) {
        if(sib_contains_ci(protocol, sib_protocols[i].keyword)) {
            if(out) *out = sib_protocols[i].info;
            return true;
        }
    }
    return false;
}
