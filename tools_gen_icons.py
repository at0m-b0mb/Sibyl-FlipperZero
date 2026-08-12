#!/usr/bin/env python3
"""Generate Sibyl's 1-bit icons from ASCII bitmaps.

'#' = foreground (black / on), anything else = background (white / off).
fbt thresholds PNGs to 1-bit, where dark pixels become 'on', and exposes each
file as `I_<basename>` via the generated sibyl_icons.h.

Every device class the classifier can name gets a 13x13 glyph, because the
result screen leads with the picture: you should know what Sibyl thinks it
heard before you have finished reading the word.

    python3 tools_gen_icons.py            # write icons/
    python3 tools_gen_icons.py --sheet    # also write a magnified contact sheet
"""
import os
import sys

from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")

# App mark: three bars of a signal fingerprint over a baseline. Deliberately
# not an antenna - Sibyl measures the shape of a signal, it does not radiate.
APP = [
    "..........",
    "....##....",
    "....##....",
    ".......##.",
    "....##.##.",
    ".##.##.##.",
    ".##.##.##.",
    ".##.##.##.",
    ".##.##.##.",
    "##########",
]

# 13x13 device glyphs, one per SibClass.
GLYPHS_13 = {
    # Garage / gate: a building with a segmented roller door.
    "dev_gate_13px": [
        "......#......",
        ".....###.....",
        "....#####....",
        "...#######...",
        "..#########..",
        ".###########.",
        ".#.........#.",
        ".#.#######.#.",
        ".#.#.....#.#.",
        ".#.#######.#.",
        ".#.#.....#.#.",
        ".#.#######.#.",
        ".###########.",
    ],
    # Car fob: the handset itself, two buttons, with the lock shackle on top.
    "dev_car_13px": [
        "....#####....",
        "...##...##...",
        "...#.....#...",
        "..#########..",
        "..#########..",
        "..##.....##..",
        "..##.###.##..",
        "..##.###.##..",
        "..##.....##..",
        "..##.###.##..",
        "..##.###.##..",
        "..##.....##..",
        "..#########..",
    ],
    # TPMS: a tyre, seen face on.
    "dev_tpms_13px": [
        "....#####....",
        "..##.....##..",
        ".#..#####..#.",
        ".#.##...##.#.",
        "##.#.....#.##",
        "##.#.....#.##",
        "##.#.....#.##",
        "##.#.....#.##",
        "##.#.....#.##",
        ".#.##...##.#.",
        ".#..#####..#.",
        "..##.....##..",
        "....#####....",
    ],
    # Weather: a thermometer.
    "dev_weather_13px": [
        "....###......",
        "...#...#.....",
        "...#.#.#.....",
        "...#.#.#.....",
        "...#.#.#.....",
        "...#.#.#.....",
        "...#.#.#.....",
        "...#.#.#.....",
        "..#..#..#....",
        ".#..###..#...",
        ".#.#####.#...",
        "..#.###.#....",
        "...#####.....",
    ],
    # Doorbell: a bell with its clapper.
    "dev_bell_13px": [
        "......#......",
        ".....###.....",
        "....#####....",
        "....#####....",
        "...#######...",
        "...#######...",
        "..#########..",
        "..#########..",
        ".###########.",
        "#############",
        ".............",
        ".....###.....",
        "......#......",
    ],
    # Remote socket: a mains plug, prongs up, cable down. Drawn as the plug
    # rather than the outlet, because an outlet face at this size reads as a
    # cartoon face and nothing else.
    "dev_socket_13px": [
        "..##.....##..",
        "..##.....##..",
        "..##.....##..",
        "#############",
        "#############",
        "#############",
        "#############",
        "#############",
        ".###########.",
        "..#########..",
        ".....###.....",
        ".....###.....",
        ".....###.....",
    ],
    # Alarm sensor: a PIR lens above the cone it watches. The cone has to be
    # a cone and not a symmetric spray, or it reads as rain falling out of the
    # weather glyph next to it.
    "dev_sensor_13px": [
        "..#########..",
        ".###########.",
        ".###########.",
        "..#########..",
        "...#######...",
        ".....#.#.....",
        "....#...#....",
        "....#...#....",
        "...#.....#...",
        "...#.....#...",
        "..#.......#..",
        "..#.......#..",
        ".#.........#.",
    ],
    # Blind motor: a window full of slats.
    "dev_blinds_13px": [
        "#############",
        "#...........#",
        "#.#########.#",
        "#...........#",
        "#.#########.#",
        "#...........#",
        "#.#########.#",
        "#...........#",
        "#.#########.#",
        "#...........#",
        "#.#########.#",
        "#...........#",
        "#############",
    ],
    # Meter: a dial with a needle.
    "dev_meter_13px": [
        ".............",
        "....#####....",
        "..##.....##..",
        ".#.........#.",
        "#.....#.....#",
        "#....##.....#",
        "#...##......#",
        "#..##.......#",
        "#...........#",
        ".#.........#.",
        "..##.....##..",
        "....#####....",
        ".............",
    ],
    # Industrial: a gear.
    "dev_industrial_13px": [
        "...##...##...",
        "...##...##...",
        ".###########.",
        ".###########.",
        "###.......###",
        "##.........##",
        "##.........##",
        "##.........##",
        "###.......###",
        ".###########.",
        ".###########.",
        "...##...##...",
        "...##...##...",
    ],
    # Unidentified.
    "dev_unknown_13px": [
        "...#######...",
        "..#########..",
        ".###.....###.",
        ".##.......##.",
        "..........##.",
        ".........###.",
        ".......####..",
        ".....####....",
        ".....##......",
        ".....##......",
        ".............",
        ".....##......",
        ".....##......",
    ],
}


def render(name, rows, size):
    for i, row in enumerate(rows):
        if len(row) != size:
            raise SystemExit(f"{name}: row {i} is {len(row)} chars, expected {size}")
    if len(rows) != size:
        raise SystemExit(f"{name}: {len(rows)} rows, expected {size}")

    img = Image.new("1", (size, size), 1)  # 1 = white background
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == "#":
                px[x, y] = 0
    path = os.path.join(OUT, f"{name}.png")
    img.save(path)
    return path


def contact_sheet(scale=8, pad=6):
    """Magnified sheet of every glyph, so the pixel art can actually be judged."""
    items = [("sibyl_10px", APP, 10)] + [(k, v, 13) for k, v in GLYPHS_13.items()]
    cols = 4
    rows = (len(items) + cols - 1) // cols
    cell = 13 * scale + pad * 2
    sheet = Image.new("RGB", (cols * cell, rows * cell), (250, 250, 250))

    for idx, (name, art, size) in enumerate(items):
        cx = (idx % cols) * cell
        cy = (idx // cols) * cell
        for y, row in enumerate(art):
            for x, ch in enumerate(row):
                colour = (20, 20, 20) if ch == "#" else (225, 225, 225)
                for dy in range(scale):
                    for dx in range(scale):
                        sheet.putpixel(
                            (cx + pad + x * scale + dx, cy + pad + y * scale + dy), colour
                        )
        print(f"  {idx:2d}  {name}")

    path = os.path.join(os.path.dirname(OUT), "images", "icon_sheet.png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sheet.save(path)
    return path


def main():
    os.makedirs(OUT, exist_ok=True)
    print(render("sibyl_10px", APP, 10))
    for name, rows in GLYPHS_13.items():
        print(render(name, rows, 13))
    if "--sheet" in sys.argv:
        print("contact sheet:", contact_sheet())


if __name__ == "__main__":
    main()
