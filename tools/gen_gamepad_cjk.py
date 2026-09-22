#!/usr/bin/env python3
"""Generate the Chinese glyph subset used by the controller overlay.

The controller panel and its prompt bar draw their own fixed-pixel text with SDL
primitives, so they cannot use the engine's TrueType stack: the overlay is
rasterized into an offscreen 640x480 canvas before it is scaled onto the frame.
The translations in ``src/gamepad_l10n.cc`` therefore ship with a subset of the
Chinese pixel font (Zpix, a 12x12 bitmap face) that contains only the glyphs
those strings actually need. The subset is a few kilobytes and needs no font
file at run time, so the overlay stays localized even when no Chinese font is
installed next to the executable.

Run this after changing any Chinese string in ``src/gamepad_l10n.cc``::

    python3 tools/gen_gamepad_cjk.py            # writes src/gamepad_cjk.h
    python3 tools/gen_gamepad_cjk.py --show 物栏  # eyeball a few glyphs

Requires Pillow (``python3 -m pip install pillow``). The generated header is
committed, so building the game never needs Python.
"""

import argparse
import os
import re
import sys

CELL_WIDTH = 12
CELL_HEIGHT = 12
# Zpix advances full-width glyphs by 13 pixels: 12 columns of ink plus one
# column of breathing room, which keeps neighbouring characters apart.
ADVANCE = 13
# The overlay's Latin font is 5x7, so a 12 pixel cell has to be lifted by three
# pixels to line both scripts up on the same visual centre.
VERTICAL_OFFSET = -3

DEFAULT_SOURCE = os.path.join("src", "gamepad_l10n.cc")
DEFAULT_FONT = os.path.join("fonts", "chs", "zpix.ttf")
DEFAULT_OUTPUT = os.path.join("src", "gamepad_cjk.h")

STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def collect_characters(path):
    """Return the non-ASCII characters used by the string literals of a source file."""
    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()

    characters = set()
    for literal in STRING_LITERAL.findall(source):
        for character in literal:
            if ord(character) > 0x7F:
                characters.add(character)

    if not characters:
        sys.exit("%s has no non-ASCII string literals - nothing to generate" % path)
    return characters


def rasterize(font, character):
    """Rasterize one character into a CELL_WIDTH x CELL_HEIGHT bitmap.

    The bitmap is placed with the glyph's own bearing, exactly as the engine's
    FreeType path would place it inside a line box, so punctuation keeps sitting
    on the baseline instead of being centred like a full-width ideograph.
    """
    mask = font.getmask(character, mode="L")
    left, top, right, bottom = font.getbbox(character)
    mask_width, mask_height = mask.size
    raw = bytes(mask)

    cell = [[0] * CELL_WIDTH for _ in range(CELL_HEIGHT)]
    for row in range(mask_height):
        for column in range(mask_width):
            value = raw[row * mask_width + column]
            if value <= 127:
                continue
            y = top + row
            x = left + column
            if 0 <= y < CELL_HEIGHT and 0 <= x < CELL_WIDTH:
                cell[y][x] = 1
    return cell


def format_cell(cell):
    rows = []
    for row in cell:
        value = 0
        for column in range(CELL_WIDTH):
            if row[column]:
                value |= 1 << (CELL_WIDTH - 1 - column)
        rows.append("0x%03X" % value)
    return ", ".join(rows)


def show(cell, character):
    print("=== %s U+%04X" % (character, ord(character)))
    for row in cell:
        print("".join("#" if pixel else "." for pixel in row))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", default=DEFAULT_SOURCE, help="source file scanned for Chinese strings")
    parser.add_argument("--font", default=DEFAULT_FONT, help="pixel font used as the glyph source")
    parser.add_argument("--output", default=DEFAULT_OUTPUT, help="generated header")
    parser.add_argument("--show", default="", help="print these characters and exit")
    arguments = parser.parse_args()

    try:
        from PIL import ImageFont
    except ImportError:
        sys.exit("Pillow is required: python3 -m pip install pillow")

    if not os.path.exists(arguments.font):
        sys.exit("font not found: %s" % arguments.font)

    font = ImageFont.truetype(arguments.font, CELL_HEIGHT)

    if arguments.show:
        for character in arguments.show:
            show(rasterize(font, character), character)
        return

    characters = sorted(collect_characters(arguments.source))
    glyphs = [(ord(character), rasterize(font, character)) for character in characters]

    empty = [chr(code) for code, cell in glyphs if not any(any(row) for row in cell)]
    if empty:
        sys.exit("these characters produced an empty bitmap: %s" % "".join(empty))

    with open(arguments.output, "w", encoding="utf-8") as handle:
        handle.write(HEADER % (os.path.basename(arguments.source), len(glyphs)))
        for code, cell in glyphs:
            handle.write("    { 0x%04X, { %s } },\n" % (code, format_cell(cell)))
        handle.write(FOOTER)

    print("wrote %s: %d glyphs from %s" % (arguments.output, len(glyphs), arguments.source))
    print("characters: %s" % "".join(characters))


HEADER = """// Generated by tools/gen_gamepad_cjk.py from the Chinese strings in %s.
// Do not edit by hand; re-run the generator after changing those strings.
//
// A 12x12 subset of the Zpix pixel font, covering exactly the characters the
// controller overlay uses. `rows[0]` is the top row and bit 11 is the leftmost
// column. Glyphs are sorted by code point so the renderer can binary search.
#ifndef FALLOUT_GAMEPAD_CJK_H_
#define FALLOUT_GAMEPAD_CJK_H_

#include <array>
#include <cstdint>

namespace fallout {
namespace pad {

struct CjkGlyph {
    uint16_t code;
    std::array<uint16_t, 12> rows;
};

inline constexpr int cjkCellWidth = 12;
inline constexpr int cjkCellHeight = 12;
// Full-width advance: the cell plus the font's one pixel side bearing.
inline constexpr int cjkAdvance = 13;
// Lifts a 12 pixel cell onto the visual centre of the overlay's 5x7 Latin font.
inline constexpr int cjkVerticalOffset = -3;

inline constexpr std::array<CjkGlyph, %d> cjkGlyphs {{
"""

FOOTER = """}};

} // namespace pad
} // namespace fallout

#endif // FALLOUT_GAMEPAD_CJK_H_
"""


if __name__ == "__main__":
    main()
