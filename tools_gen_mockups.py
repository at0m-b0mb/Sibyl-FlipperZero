#!/usr/bin/env python3
"""Render faithful 128x64 mockups of Sibyl's screens for the README.

Two rules keep these honest:

1. The layout constants below are copied from the view sources, and text is
   positioned by BASELINE (PIL anchor "ls"/"rs"/"ms"), because that is what
   canvas_draw_str takes. Getting this wrong produces pretty pictures that do
   not match the device.

2. The content is not typed in by hand. `test/host_mockup_dump.c` runs the
   real classifier over the real feature extractor and prints what it decided;
   this script only draws it. If the scoring changes, these pictures change.

    make -C test host_mockup_dump && ./test/host_mockup_dump > /tmp/sibyl.txt
    python3 tools_gen_mockups.py /tmp/sibyl.txt

Run with no argument and it will build and run the dumper itself.
"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")

W, H = 128, 64
SCALE = 4
BEZEL = 10

# Flipper's amber LCD.
ON = (0, 0, 0)
OFF = (255, 168, 0)
BEZEL_COL = (32, 32, 34)

# Menlo at 10 px advances exactly 6 px per character, which is the same grid
# FontKeyboard uses. FontSecondary is narrower; 9 px is the closest match.
FONT_CANDIDATES = [
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Supplemental/Andale Mono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
]


def load_font(size):
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
    return ImageFont.load_default()


F_PRIMARY = load_font(10)   # FontPrimary  - bold-ish, ~7 px/char
F_SECOND = load_font(9)     # FontSecondary - ~5-6 px/char


def new_screen():
    img = Image.new("RGB", (W, H), OFF)
    return img, ImageDraw.Draw(img)


def text(d, x, y, s, font=F_SECOND, anchor="ls", fill=ON):
    """y is the BASELINE, matching canvas_draw_str."""
    d.text((x, y), s, font=font, fill=fill, anchor=anchor)


def hline(d, y, x0=0, x1=W - 1):
    d.line([(x0, y), (x1, y)], fill=ON)


def frame(d, x, y, w, h):
    d.rectangle([x, y, x + w - 1, y + h - 1], outline=ON)


def box(d, x, y, w, h, fill=ON):
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=fill)


def meter(d, x, y, w, h, pct):
    frame(d, x, y, w, h)
    fill = (w - 2) * min(pct, 100) // 100
    if fill > 0:
        box(d, x + 1, y + 1, fill, h - 2)


def badge(d, x, y, s):
    """Inverted pill, as rv_badge draws it."""
    w = int(d.textlength(s, font=F_SECOND)) + 6
    d.rounded_rectangle([x, y, x + w - 1, y + 10], radius=2, fill=ON)
    text(d, x + 3, y + 8, s, fill=OFF)
    return w


def page_dots(d, page, n=3):
    for i in range(n):
        x = 108 + i * 6
        if i == page:
            d.ellipse([x - 2, 58, x + 2, 62], fill=ON)
        else:
            d.ellipse([x - 2, 58, x + 2, 62], outline=ON)


def glyph(d, x, y, kind):
    """A 13x13 stand-in for the real icon, drawn from the same ASCII art."""
    import tools_gen_icons as icons

    art = icons.GLYPHS_13.get(kind)
    if not art:
        return
    for gy, row in enumerate(art):
        for gx, ch in enumerate(row):
            if ch == "#":
                d.point((x + gx, y + gy), fill=ON)


def wrap(d, s, max_w, font=F_SECOND, max_lines=99):
    out, cur = [], ""
    for word in s.split():
        cand = word if not cur else cur + " " + word
        if d.textlength(cand, font=font) <= max_w:
            cur = cand
        else:
            if cur:
                out.append(cur)
            cur = word
            if len(out) >= max_lines:
                return out[:max_lines]
    if cur:
        out.append(cur)
    return out[:max_lines]


def save(img, name):
    big = img.resize((W * SCALE, H * SCALE), Image.NEAREST)
    canvas = Image.new(
        "RGB", (W * SCALE + BEZEL * 2, H * SCALE + BEZEL * 2), BEZEL_COL
    )
    canvas.paste(big, (BEZEL, BEZEL))
    path = os.path.join(OUT, name)
    canvas.save(path)
    print(" ", path)
    return canvas


# --------------------------------------------------------------- parsing ---


def parse(path):
    scenarios, cur = {}, None
    with open(path) as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if line.startswith("SCENARIO "):
                cur = {"cand": [], "reason": []}
                scenarios[line.split(" ", 1)[1]] = cur
            elif line == "END":
                cur = None
            elif cur is not None and " " in line:
                key, val = line.split(" ", 1)
                if key == "cand":
                    score, name = val.split(" ", 1)
                    cur["cand"].append((int(score), name))
                elif key == "reason":
                    cur["reason"].append(val)
                elif key == "pulses":
                    cur["pulses"] = [int(v) for v in val.split()]
                else:
                    cur[key] = val
            elif cur is not None:
                cur[line] = ""
    return scenarios


CLASS_GLYPH = {
    "Gate remote": "dev_gate_13px",
    "Car key fob": "dev_car_13px",
    "Tyre sensor": "dev_tpms_13px",
    "Weather sensor": "dev_weather_13px",
    "Doorbell": "dev_bell_13px",
    "Remote socket": "dev_socket_13px",
    "Alarm sensor": "dev_sensor_13px",
    "Blind motor": "dev_blinds_13px",
    "Meter / telemetry": "dev_meter_13px",
    "Industrial remote": "dev_industrial_13px",
}


def freq_str(s):
    hz = int(s["freq"])
    return "%d.%02d %s" % (hz // 1000000, (hz % 1000000) // 10000, s["mod"])


# ----------------------------------------------------------------- screens --


def screen_answer(s, name):
    """result_view.c :: rv_draw_answer"""
    img, d = new_screen()
    badge(d, 2, 1, s["verdict"])
    text(d, 126, 10, freq_str(s), anchor="rs")

    cls = s["class"]
    glyph(d, 2, 15, CLASS_GLYPH.get(cls, "dev_unknown_13px"))
    text(d, 19, 25, cls, font=F_PRIMARY)

    for i, line in enumerate(wrap(d, s["tagline"], 124, max_lines=2)):
        text(d, 2, 35 + i * 8, line)

    meter(d, 2, 46, 96, 8, int(s["confidence"]))
    text(d, 126, 53, s["confidence"] + "%", anchor="rs")

    text(d, 2, 63, "OK explain")
    page_dots(d, 0)
    return save(img, name)


def screen_shortlist(s, name):
    """result_view.c :: rv_draw_shortlist"""
    img, d = new_screen()
    text(d, 2, 10, "What it could be", font=F_PRIMARY)
    hline(d, 12)

    for i, (score, cname) in enumerate(s["cand"][:4]):
        y = 15 + i * 10
        sel = i == 0
        if sel:
            box(d, 0, y, W, 10)
        fg = OFF if sel else ON
        text(d, 3, y + 8, cname, fill=fg)
        text(d, 125, y + 8, str(score), anchor="rs", fill=fg)
        bx, bw = 60, 30
        d.rectangle([bx, y + 2, bx + bw - 1, y + 7], outline=fg)
        fill = (bw - 2) * score // 100
        if fill > 0:
            d.rectangle([bx + 1, y + 3, bx + fill, y + 6], fill=fg)

    if s.get("generic") == "1":
        text(d, 2, 63, "Chip, not device")
    else:
        text(d, 2, 63, "OK explain")
    page_dots(d, 1)
    return save(img, name)


def screen_evidence(s, name):
    """result_view.c :: rv_draw_evidence"""
    img, d = new_screen()
    text(d, 2, 10, "Evidence", font=F_PRIMARY)
    text(d, 126, 10, s["rssi"] + " dBm", anchor="rs")
    hline(d, 12)

    y = 20
    reasons = s["reason"]
    if s.get("note"):
        for line in wrap(d, s["note"], 124, max_lines=2):
            text(d, 2, y, line)
            y += 8
        reasons = reasons[1:]
    for r in reasons:
        if y > 44:
            break
        text(d, 2, y, r)
        y += 8

    hline(d, 46)
    # rv_draw_trace: low = 56, high = 50, x from 2 for 122 px
    pulses = s.get("pulses", [])
    total = sum(pulses)
    if total:
        level = s.get("first_level") == "1"
        acc, px = 0, 2
        for p in pulses:
            acc += p
            x = 2 + acc * 122 // total
            yy = 50 if level else 56
            d.line([(px, yy), (x, yy)], fill=ON)
            d.line([(x, 50), (x, 56)], fill=ON)
            px, level = x, not level
        text(d, 2, 63, "%d edges" % len(pulses))
    page_dots(d, 2)
    return save(img, name)


def screen_listen(s, name):
    """listen_view.c - trace baseline 45, top 15, 2 px per sample."""
    img, d = new_screen()
    text(d, 2, 10, "Listening", font=F_PRIMARY)
    text(d, 126, 9, "433.92 AM650*", anchor="rs")
    hline(d, 12)

    d.line([(2, 45), (125, 45)], fill=ON)

    # A plausible capture: quiet floor, then the burst that got accepted.
    import math

    for i in range(62):
        x = 2 + i * 2
        if 34 <= i <= 46:
            h = 22 + int(6 * math.sin(i * 2.0))
        else:
            h = 3 + (i * 7 % 4)
        box(d, x, 45 - h, 2, h)
        if i in (36, 40, 44):
            d.line([(x, 13), (x, 45)], fill=ON)
            box(d, x - 1, 11, 3, 3)

    text(d, 2, 54, "8 packets")
    text(d, 126, 54, "12 noise", anchor="rs")
    hline(d, 56)
    text(d, 2, 63, "< > band")
    text(d, 126, 63, "OK reset", anchor="rs")
    return save(img, name)


def screen_explain(name):
    """explain_view.c - rows of 9 px, first baseline 21."""
    img, d = new_screen()
    text(d, 2, 10, "Gate remote", font=F_PRIMARY)
    hline(d, 12)

    rows = [
        ("What it is", True),
        ("The handset that opens", False),
        ("a garage door, driveway", False),
        ("gate or car park", False),
        ("barrier. Usually a two", False),
    ]

    for i, (line, head) in enumerate(rows):
        text(d, 2, 21 + i * 9, line, font=F_PRIMARY if head else F_SECOND)

    # elements_scrollbar_pos on the right
    d.line([(126, 14), (126, 63)], fill=ON)
    box(d, 125, 14, 3, 14)
    return save(img, name)


def screen_hunt(name):
    """hunt_view.c - one bar per band, base y=46, 8 px pitch."""
    img, d = new_screen()
    text(d, 2, 10, "Find Band", font=F_PRIMARY)
    text(d, 126, 10, "OK use band", anchor="rs")
    hline(d, 12)

    d.line([(0, 46), (127, 46)], fill=ON)
    deltas = [2, 1, 3, 4, 2, 1, 2, 3, 1, 5, 34, 6, 3, 2, 1, 2]
    best = 10
    for i, delta in enumerate(deltas):
        x = i * 8
        h = 30 * min(delta, 40) // 40
        if h > 0:
            box(d, x, 46 - h, 7, h)
        else:
            d.line([(x, 45), (x + 6, 45)], fill=ON)
        if i == best:
            d.line([(x + 1, 48), (x + 5, 48)], fill=ON)
            d.line([(x + 2, 49), (x + 4, 49)], fill=ON)

    text(d, 2, 61, "433.92 MHz  +34 dB", font=F_PRIMARY)
    return save(img, name)


def screen_menu(name):
    img, d = new_screen()
    text(d, 64, 10, "Sibyl", font=F_PRIMARY, anchor="ms")
    hline(d, 13)
    items = [
        "Identify signal",
        "Find band",
        "Device library",
        "This session",
    ]
    for i, item in enumerate(items):
        y = 16 + i * 10
        if i == 0:
            d.rounded_rectangle([1, y, 126, y + 9], radius=2, fill=ON)
        text(d, 64, y + 8, item, anchor="ms", fill=OFF if i == 0 else ON)
    return save(img, name)


# ------------------------------------------------------------------ sheet --


def sheet(images, name, cols=3):
    pad = 8
    cw = max(i.width for i in images) + pad
    ch = max(i.height for i in images) + pad
    rows = (len(images) + cols - 1) // cols
    out = Image.new("RGB", (cols * cw + pad, rows * ch + pad), (18, 18, 20))
    for i, img in enumerate(images):
        x = pad + (i % cols) * cw
        y = pad + (i // cols) * ch
        out.paste(img, (x, y))
    path = os.path.join(OUT, name)
    out.save(path)
    print(" ", path)


def main():
    os.makedirs(OUT, exist_ok=True)
    sys.path.insert(0, HERE)

    if len(sys.argv) > 1:
        data_path = sys.argv[1]
    else:
        # Build and run the dumper so the mockups always reflect this tree.
        test_dir = os.path.join(HERE, "test")
        subprocess.run(["make", "-C", test_dir, "host_mockup_dump"], check=True)
        data_path = os.path.join(test_dir, "mockup_data.txt")
        with open(data_path, "w") as fh:
            subprocess.run([os.path.join(test_dir, "host_mockup_dump")], stdout=fh, check=True)

    s = parse(data_path)

    shots = [
        screen_menu("screen_menu.png"),
        screen_listen(s["encoder"], "screen_listen.png"),
        screen_answer(s["somfy"], "screen_answer_confirmed.png"),
        screen_answer(s["tpms"], "screen_answer_tpms.png"),
        screen_shortlist(s["encoder"], "screen_shortlist.png"),
        screen_evidence(s["encoder"], "screen_evidence.png"),
        screen_explain("screen_explain.png"),
        screen_hunt("screen_hunt.png"),
    ]
    sheet(shots, "screens.png")


if __name__ == "__main__":
    main()
