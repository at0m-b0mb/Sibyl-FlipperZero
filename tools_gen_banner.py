#!/usr/bin/env python3
"""Render Sibyl's README banner and GitHub social preview.

The waveform across the bottom is not decorative filler: it is the real pulse
train from `test/host_mockup_dump`, the same 24-bit OOK packet the mockups are
drawn from, plotted as the app plots it. If the generator changes, the banner
changes.

    python3 tools_gen_banner.py
"""
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "images")

BG = (13, 15, 18)
GRID = (26, 30, 36)
AMBER = (255, 168, 0)
AMBER_DIM = (150, 99, 0)
WHITE = (238, 240, 244)
GREY = (138, 146, 158)

FONTS = {
    "bold": [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    ],
    "regular": [
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ],
    "mono": [
        "/System/Library/Fonts/Menlo.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    ],
}


def font(kind, size):
    for path in FONTS[kind]:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
    return ImageFont.load_default()


def load_pulses():
    """The real capture, straight out of the engine's own dump."""
    test_dir = os.path.join(HERE, "test")
    data = os.path.join(test_dir, "mockup_data.txt")
    if not os.path.exists(data):
        subprocess.run(["make", "-C", test_dir, "host_mockup_dump"], check=True)
        with open(data, "w") as fh:
            subprocess.run([os.path.join(test_dir, "host_mockup_dump")], stdout=fh, check=True)

    pulses, level = [], True
    with open(data) as fh:
        for line in fh:
            if line.startswith("pulses "):
                pulses = [int(v) for v in line.split()[1:]]
                break
            if line.startswith("first_level "):
                level = line.split()[1] == "1"
    return pulses, level


def draw_grid(d, w, h, step=40):
    for x in range(0, w, step):
        d.line([(x, 0), (x, h)], fill=GRID)
    for y in range(0, h, step):
        d.line([(0, y), (w, y)], fill=GRID)


def draw_trace(d, pulses, level, x0, y_hi, y_lo, width, colour, thickness=3):
    total = sum(pulses)
    if not total:
        return
    acc, px = 0, x0
    for p in pulses:
        acc += p
        x = x0 + acc * width // total
        y = y_hi if level else y_lo
        d.line([(px, y), (x, y)], fill=colour, width=thickness)
        d.line([(x, y_lo), (x, y_hi)], fill=colour, width=thickness)
        px, level = x, not level


def draw_mark(d, x, y, unit=10, colour=AMBER):
    """The app icon's three bars, scaled up."""
    bars = [(1, 4), (4, 8), (7, 6)]  # (column, height in units)
    for col, hgt in bars:
        d.rectangle(
            [x + col * unit, y - hgt * unit, x + (col + 2) * unit - 1, y - 1], fill=colour
        )
    d.rectangle([x, y, x + 9 * unit - 1, y + unit - 1], fill=colour)


def banner(w=1280, h=440):
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img)
    draw_grid(d, w, h)

    pulses, level = load_pulses()

    # The captured packet, as a signal running along the top edge.
    draw_trace(d, pulses, level, 0, 26, 62, w, (74, 50, 8), thickness=4)

    draw_mark(d, 92, 206, unit=11)

    d.text((92, 224), "SIBYL", font=font("bold", 108), fill=WHITE)
    d.text((100, 340), "Shazam for RF", font=font("regular", 40), fill=AMBER)

    d.text(
        (104, 394),
        "Capture any Sub-GHz signal and find out what kind of device sent it",
        font=font("regular", 24),
        fill=GREY,
    )

    # Verdict chips, right-hand side, in the app's own language.
    chips = [
        ("CONFIRMED", AMBER, BG),
        ("LIKELY", None, AMBER),
        ("POSSIBLE", None, GREY),
    ]
    cx, cy = w - 360, 130
    f = font("mono", 26)
    for label, fill, fg in chips:
        tw = int(d.textlength(label, font=f))
        d.rounded_rectangle(
            [cx, cy, cx + tw + 34, cy + 46],
            radius=10,
            fill=fill,
            outline=fg if fill is None else None,
            width=2,
        )
        d.text((cx + 17, cy + 11), label, font=f, fill=fg)
        cy += 62

    # The device classes it can name.
    d.text(
        (w - 360, 330),
        "gate  car fob  tyre  weather\nbell  socket  sensor  blind\nmeter  industrial",
        font=font("mono", 20),
        fill=AMBER_DIM,
        spacing=12,
    )

    path = os.path.join(OUT, "banner.png")
    img.save(path)
    print(" ", path)
    return img


def social(w=1280, h=640):
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img)
    draw_grid(d, w, h, step=48)

    pulses, level = load_pulses()
    draw_trace(d, pulses, level, 0, 30, 72, w, (74, 50, 8), thickness=5)

    draw_mark(d, w // 2 - 50, 214, unit=11)

    title = "SIBYL"
    f = font("bold", 132)
    tw = int(d.textlength(title, font=f))
    d.text(((w - tw) // 2, 236), title, font=f, fill=WHITE)

    sub = "Shazam for RF"
    f2 = font("regular", 46)
    tw2 = int(d.textlength(sub, font=f2))
    d.text(((w - tw2) // 2, 388), sub, font=f2, fill=AMBER)

    line = "Sub-GHz signal classifier for Flipper Zero"
    f3 = font("regular", 26)
    tw3 = int(d.textlength(line, font=f3))
    d.text(((w - tw3) // 2, 456), line, font=f3, fill=GREY)

    tag = "listen only  -  never transmits"
    f4 = font("mono", 22)
    tw4 = int(d.textlength(tag, font=f4))
    d.text(((w - tw4) // 2, 566), tag, font=f4, fill=AMBER_DIM)

    path = os.path.join(OUT, "social-preview.png")
    img.save(path)
    print(" ", path)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    banner()
    social()


if __name__ == "__main__":
    sys.exit(main())
