#!/usr/bin/env python3
"""Regenerate src/ui/font.h from third_party/monogram/monogram.ttf.

The UI draws text on a fixed 8x8 cell grid (Renderer::drawChar), so the
table is a plain bitmap: one entry per character, each an ASCII-art block
that Renderer stamps pixel by pixel. This script rasterizes monogram onto
that grid rather than hand-drawing it.

Why ppem 16: monogram is a pixel font whose design grid lands exactly on
whole pixels at 16, giving 5x7 caps and digits -- the same box the old
hand-drawn table used. Rasterizing at any other size lands off-grid and
FreeType drops whole stems ('X' loses its crossing, 'M' its middle), which
looks like a broken font but is a broken raster. Do not "tune" this value.

Why 8 rows and not 7: the cell is 8px tall but the old table was 7, so the
bottom row was permanently blank. monogram's descenders need rows 7 and 8,
so they get folded onto row 7 (see DESCENDERS) -- one row of tail instead
of two. That keeps ',' ';' and 'Q', which only reach row 7, exactly as
drawn. The meter and curve glyphs below deliberately stay within rows 0..6
so the seven-level arithmetic in MixerScreen and EqScreen is unaffected.

Usage:  python tools/gen_font.py [--check]
        --check exits non-zero if src/ui/font.h is stale instead of writing.
"""
import argparse
import os
import sys

from PIL import ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TTF = os.path.join(ROOT, "third_party", "monogram", "monogram.ttf")
OUT = os.path.join(ROOT, "src", "ui", "font.h")

PPEM = 16
GW, GH = 5, 8          # glyph box: 5 wide (Renderer draws 5 cols), 8 rows
BSL = chr(92)

# Characters the old table carried, in its order. Kept deliberately: the UI
# only ever draws these, and Renderer::drawChar silently ignores anything
# missing, so adding coverage is safe but dropping it is not.
UPPER = [chr(c) for c in range(ord("A"), ord("Z") + 1)]
LOWER = [chr(c) for c in range(ord("a"), ord("z") + 1)]
DIGITS = [chr(c) for c in range(ord("0"), ord("9") + 1)]
SYMBOLS = ["!", ".", ",", "?", "%", "#", "_", "-", ";", "`", "=", ":",
           "<", ">", "~", "*", "/", "'", '"', "(", ")", "}", "{", "+"]

# Descenders folded from monogram's rows 7..8 onto row 7 alone.
#
# 'g' and 'q' share an identical bowl in monogram and are told apart purely
# by the tail -- g hooks left, q drops straight -- so the fold has to keep
# that distinction or the two collapse into the same glyph.
#
# The tails are deliberately narrower than monogram's own. Row 7 used to be
# the blank gap between lines, so any ink there now sits directly above the
# next line's caps; the sample browser stacks raw lowercase filenames
# (FileBrowser FILE mode does not uppercase them), which is exactly where
# that shows. Trimming the hooks from three pixels to two keeps the tail
# legible while cutting the collision mass against the row below.
DESCENDERS = {
    "g": "  ## ",   # hook left, from "    #" + " ### "
    "q": "    #",   # straight drop, from "    #" + "    #"
    "p": "#    ",   # stem, from "#    " + "#    "
    "y": "  ## ",   # hook left, mirrors g
    "j": "  ## ",   # hook left, from "#   #" + " ### "
}

# Meter fill glyphs 0x01..0x07 and curve plot glyphs 0x08..0x0E, verbatim
# from the old table. Seven rows each, blank row 7, because MixerScreen and
# EqScreen both index them as exactly seven levels per cell.
METERS = [(i, [" " * 5] * (7 - i) + ["#" * 5] * i) for i in range(1, 8)]
CURVES = [(0x08 + i, [" " * 5] * i + ["#" * 5] + [" " * 5] * (6 - i))
          for i in range(7)]

HEADER = '''/*
	"font.h" -- generated, do not edit by hand.

	Regenerate with:  python tools/gen_font.py
	Source glyphs:    third_party/monogram/monogram.ttf (CC0)

	This file is C89.

	Compiler/preprocessor configs (optional):
	-D NO_LOWERCASE
	-D NO_SYMBOLS
	-D NO_NUMBERS
	In case you are limited in space :)
*/
#ifndef FONT_H
#define FONT_H

struct Font {
    char letter;
    char code[8][6];
};

struct Font font[] = {
'''

METER_COMMENT = '''
/* ------------------------------------------------------------------
   Meter fill glyphs, 0x01..0x07 (MIXER_SPEC.md 5.1).

   A bar is a stack of cells; each cell holds one of these, so 0x07 is a
   full cell and a partial cell tops the stack off. Seven levels because
   the fill is seven pixel rows tall -- one level per row, which is the
   finest this font can express and costs no more than the four-level
   version would. Row 7 stays blank on purpose: text glyphs use it for
   descenders, but a meter that used it would make the level arithmetic
   in MixerScreen eight-valued and break every stored bar height.

   Deliberately NOT printable characters: nothing else in the UI draws
   these, so a meter can never be confused with text. Renderer::drawChar
   ignores unknown characters silently (it returns before even stamping
   the shadow grid), so these must stay in this table or meters vanish
   with no error. dumpScreenText() maps them back to '1'..'7' so headless
   dumps stay readable.
   ------------------------------------------------------------------ */
'''

CURVE_COMMENT = '''
/* ------------------------------------------------------------------
   Curve plot glyphs, 0x08..0x0E (EQ_SPEC.md 6).

   A dash at one of the seven pixel rows, so a graph N cells tall has
   7N vertical positions to plot on. The EQ editor draws its response
   curve one character per column with these -- not with line drawing,
   which bypasses the shadow grid entirely and would make the curve
   invisible to every dump, golden and assertion. Row 7 is blank for the
   same reason as the meter fills above.

   Same reservation logic as the meter fills above: nothing else in
   the UI draws these, so a curve can never be confused with text.
   dumpScreenText() maps them to 'a'..'g'.
   ------------------------------------------------------------------ */
'''


def raw_ink(font, ch):
    """Ink pixels for `ch` in FreeType's own coordinates.

    getmask2 returns the bitmap plus its offset from the "la" origin, which
    sits at the ascender -- well above the tallest glyph. The offsets are
    only meaningful relative to each other, so callers must normalize the
    whole charset together (see normalize) rather than per glyph, or every
    glyph gets flattened onto the same top edge.
    """
    mask, (ox, oy) = font.getmask2(ch, mode="1")
    w, h = mask.size
    return [(x + ox, y + oy) for y in range(h) for x in range(w)
            if mask.getpixel((x, y))]


def normalize(inks):
    """Shift every glyph by one common origin so the tallest sits at row 0."""
    allink = [p for v in inks.values() for p in v]
    minx = min(p[0] for p in allink)
    miny = min(p[1] for p in allink)
    return {c: [(x - minx, y - miny) for x, y in v] for c, v in inks.items()}


def to_grid(ink):
    """Lay normalized ink onto the 5x8 box; report anything that won't fit."""
    grid = [[" "] * GW for _ in range(GH)]
    overflow = []
    for gx, gy in ink:
        if 0 <= gx < GW and 0 <= gy < GH:
            grid[gy][gx] = "#"
        else:
            overflow.append((gx, gy))
    return ["".join(r) for r in grid], overflow


def c_literal(ch):
    if ch == "'":
        return "'" + BSL + "'" + "'"
    if ch == BSL:
        return "'" + BSL + BSL + "'"
    return "'%s'" % ch


def entry(key, rows):
    body = (",\n".join('"%s"' % r for r in rows))
    return "{%s, {\n%s}},\n" % (key, body)


def build():
    font = ImageFont.truetype(TTF, PPEM)
    charset = UPPER + LOWER + DIGITS + SYMBOLS
    inks = normalize({ch: raw_ink(font, ch) for ch in charset})
    out = [HEADER]
    problems = []

    def emit(ch):
        rows, overflow = to_grid(inks[ch])
        if ch in DESCENDERS:
            # Fold the two-row tail onto row 7; rows 0..6 are monogram's.
            rows = rows[:7] + [DESCENDERS[ch]]
        elif overflow:
            problems.append("%r overflows the 5x8 box at %s" % (ch, overflow))
        return entry(c_literal(ch), rows)

    out.append(entry("' '", [" " * GW] * GH))
    for ch in UPPER:
        out.append(emit(ch))

    out.append("#ifndef NO_LOWERCASE\n")
    for ch in LOWER:
        out.append(emit(ch))
    out.append("#endif\n")

    out.append("#ifndef NO_NUMBERS\n")
    for ch in DIGITS:
        out.append(emit(ch))
    out.append("#endif\n")

    out.append("#ifndef NO_SYMBOLS\n")
    for ch in SYMBOLS:
        out.append(emit(ch))
    out.append("#endif\n")

    out.append(METER_COMMENT)
    for code, rows in METERS:
        out.append(entry("0x%02X" % code, rows + [" " * GW]))

    out.append(CURVE_COMMENT)
    for code, rows in CURVES:
        out.append(entry("0x%02X" % code, rows + [" " * GW]))

    out.append("\n/* Fallback/end Char. If you don't know the\n"
               "font size, use this as the \"null terminator\" */\n")
    out.append(entry("0", ["#" * GW] * GH))
    out.append("};\n\n#endif\n")
    return "".join(out), problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    text, problems = build()
    if problems:
        for p in problems:
            sys.stderr.write("WARN: %s\n" % p)

    if args.check:
        current = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if current.replace(chr(13), "") != text:
            sys.stderr.write("font.h is stale; run: python tools/gen_font.py\n")
            return 1
        print("font.h up to date")
        return 0

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s (%d bytes)" % (OUT, len(text)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
