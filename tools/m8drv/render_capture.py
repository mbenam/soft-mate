#!/usr/bin/env python3
"""Render an `m8_nav --ui-capture` JSON as a standalone HTML page.

Why this exists, specifically
----------------------------
The driver decides where the cursor is by matching a colour. When that match is
wrong, every symptom points somewhere else: cursor reads return -1, `probe`
reports presses that are not landing, and the device looks broken while it is
working perfectly (hw_findings.md UI-14). The text dump could not show this,
because the text is identical either way -- the *colour* is the whole story, and
the colour was the one thing no view in the toolchain rendered.

So this page draws two things at once: what the device actually put on screen,
and what the driver believes about it. A cell the driver thinks is the cursor is
outlined. If the accent is on no cell, the page says so at the top instead of
rendering a screen that looks perfectly fine and leaves you to wonder.

Stdlib only, on purpose: m8drv.py takes no third-party dependency, and a
diagnostic that needs an install is one you skip at the moment you need it. The
output is a file rather than a window because the tooling is driven headlessly --
an artifact can be attached to a report, diffed, and read by whoever is not
sitting at the device.

Usage
-----
    m8_nav --port COM3 --ui-capture cap.json
    python tools/m8drv/render_capture.py cap.json -o cap.html

    # two captures side by side, e.g. before/after a key press
    python tools/m8drv/render_capture.py before.json after.json -o diff.html
"""
from __future__ import annotations

import argparse
import html
import json
import os
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# Fallback accent, kept in step with ScreenGrid::cursorColor. Only used when no
# pinned hw_theme.json is present -- and the page labels it as unconfirmed,
# because "the value we guessed" and "the value the device confirmed" must never
# look alike in a diagnostic.
STOCK_ACCENT = [0, 240, 248]
STOCK_TOL = 16


def load_accent(path: str = "hw_theme.json"):
    """(accent, tolerance, source) from the pinned theme file, or the fallback."""
    try:
        with open(os.path.join(REPO_ROOT, path), encoding="utf-8") as f:
            d = json.load(f)
    except (OSError, ValueError):
        return STOCK_ACCENT, STOCK_TOL, "built-in default (UNCONFIRMED)"
    if not d.get("pinned"):
        return STOCK_ACCENT, STOCK_TOL, "built-in default (hw_theme.json is not pinned)"
    accent = d.get("accent")
    if not (isinstance(accent, list) and len(accent) == 3):
        return STOCK_ACCENT, STOCK_TOL, "built-in default (hw_theme.json malformed)"
    tol = d.get("tolerance", STOCK_TOL)
    if not isinstance(tol, int):
        tol = STOCK_TOL
    return accent, tol, "hw_theme.json (pinned, theme_id %r)" % d.get("theme_id")


def load_capture(path: str) -> dict:
    # strict=False tolerates the raw control bytes written by an m8_nav older
    # than 2026-08-18; newer captures escape them and parse either way.
    with open(path, encoding="utf-8", errors="replace") as f:
        return json.loads(f.read(), strict=False)


def rgb(c) -> str:
    return "rgb(%d,%d,%d)" % (c[0], c[1], c[2])


def near(a, b, tol: int) -> bool:
    return all(abs(a[i] - b[i]) <= tol for i in range(3))


def render_one(cap: dict, accent, tol: int, title: str) -> str:
    pal = cap.get("palette") or []
    cells = cap.get("cells") or []
    rects = cap.get("rects") or []

    accent_idx = next((i for i, c in enumerate(pal)
                       if len(c) == 3 and near(c, accent, tol)), None)

    grid: dict = {}
    for c in cells:
        grid[(c.get("row", 0), c.get("col", 0))] = c
    rows = [r for (r, _) in grid] or [0]
    cols = [c for (_, c) in grid] or [0]
    max_row, max_col = max(rows), max(cols)

    # Sliders are drawn as filled rects rather than characters, so a page that
    # skipped them would misrepresent every screen carrying a value bar.
    bars: dict = {}
    for r in rects:
        bars.setdefault((r.get("row", 0), r.get("col", 0)), []).append(r)

    out = ['<section class="cap">']
    out.append('<h2>%s</h2>' % html.escape(title))

    if accent_idx is None:
        out.append(
            '<p class="alarm"><strong>Accent %s is on no cell of this screen.</strong> '
            'Every cursor query here returns -1, and the driver will report presses '
            'that are not landing. This is a theme mismatch, not a device fault &mdash; '
            'run <code>m8_nav --pin-theme</code>.</p>' % html.escape(str(accent)))
    else:
        n = sum(1 for c in cells
                if c.get("fg") == accent_idx or c.get("bg") == accent_idx)
        out.append('<p class="ok">Accent found as palette index %d, on %d cell%s '
                   '&mdash; outlined below.</p>' % (accent_idx, n, "" if n == 1 else "s"))

    meta = [("screen", cap.get("screen")), ("theme_id", cap.get("theme_id")),
            ("firmware", cap.get("firmware")), ("font_mode", cap.get("font_mode")),
            ("settled", cap.get("settled")),
            ("cells", len(cells)), ("rects", len(rects))]
    out.append('<p class="meta">' + " &middot; ".join(
        "%s <b>%s</b>" % (html.escape(str(k)), html.escape(str(v))) for k, v in meta) + '</p>')

    out.append('<div class="screen">')
    for row in range(max_row + 1):
        out.append('<div class="row">')
        for col in range(max_col + 1):
            cell = grid.get((row, col))
            ch = (cell or {}).get("ch", " ")
            fg = (cell or {}).get("fg", -1)
            bg = (cell or {}).get("bg", -1)
            style = []
            if 0 <= fg < len(pal):
                style.append("color:%s" % rgb(pal[fg]))
            if 0 <= bg < len(pal):
                style.append("background:%s" % rgb(pal[bg]))
            klass = "c"
            if accent_idx is not None and (fg == accent_idx or bg == accent_idx):
                klass += " cur"
            bar = ""
            for r in bars.get((row, col), []):
                st = r.get("style", -1)
                bar = ('<i style="width:%dpx;height:%dpx;left:%dpx;top:%dpx;background:%s"></i>'
                       % (r.get("w_px", 0), r.get("h_px", 0), r.get("off_x", 0),
                          r.get("off_y", 0),
                          rgb(pal[st]) if 0 <= st < len(pal) else "#888"))
            disp = html.escape(ch) if ch and ch.strip() else "&nbsp;"
            out.append('<span class="%s" style="%s">%s%s</span>'
                       % (klass, ";".join(style), disp, bar))
        out.append('</div>')
    out.append('</div>')

    # Palette with usage counts. The cursor accent is always a low-count entry,
    # so seeing the counts is how you spot the right colour by eye when the
    # pinned one is wrong.
    use = {}
    for c in cells:
        for k in ("fg", "bg"):
            i = c.get(k, -1)
            if 0 <= i < len(pal):
                use[i] = use.get(i, 0) + 1
    out.append('<table class="pal"><tr><th></th><th>idx</th><th>rgb</th>'
               '<th>cells</th><th></th></tr>')
    for i, c in enumerate(pal):
        out.append('<tr><td><span class="sw" style="background:%s"></span></td>'
                   '<td>%d</td><td>%s</td><td>%d</td><td>%s</td></tr>'
                   % (rgb(c), i, html.escape(str(c)), use.get(i, 0),
                      "&larr; accent" if i == accent_idx else ""))
    out.append('</table></section>')
    return "\n".join(out)


CSS = """
:root { --bg:#111316; --fg:#e6e9ef; --line:#2a2f37; --alarm:#ff5c5c; --ok:#3ddc84; }
* { box-sizing: border-box; }
body { margin:0; padding:24px; background:var(--bg); color:var(--fg);
       font:14px/1.5 ui-sans-serif,system-ui,sans-serif; }
h1 { font-size:18px; margin:0 0 4px; }
h2 { font-size:15px; margin:0 0 8px; font-weight:600; }
.sub { color:#98a2b3; margin:0 0 20px; }
.wrap { display:flex; gap:24px; flex-wrap:wrap; align-items:flex-start; }
.cap { border:1px solid var(--line); border-radius:8px; padding:16px;
       background:#0b0d10; max-width:100%; overflow-x:auto; }
.meta { color:#98a2b3; font-size:12px; margin:0 0 12px; }
.alarm { color:var(--alarm); border-left:3px solid var(--alarm); padding-left:10px; }
.ok { color:var(--ok); font-size:13px; }
.screen { background:#000; padding:8px; display:inline-block; border-radius:4px; }
.row { display:flex; height:20px; }
.c { position:relative; width:16px; height:20px; font:13px/20px ui-monospace,
     "Cascadia Mono",Consolas,monospace; text-align:center; white-space:pre; }
/* The driver's belief, drawn over the device's pixels. Where the outline is not
   where the cursor visibly is, that gap is the bug. */
.cur { outline:1px solid rgba(255,255,255,.55); outline-offset:-1px; }
.c i { position:absolute; display:block; }
.pal { border-collapse:collapse; margin-top:14px; font-size:12px; }
.pal th, .pal td { padding:2px 10px 2px 0; text-align:left; color:#98a2b3;
                   font-weight:400; }
.sw { display:inline-block; width:14px; height:14px; border:1px solid var(--line);
      vertical-align:middle; }
code { background:#1b1f26; padding:1px 5px; border-radius:3px; }
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("captures", nargs="+", help="one or more ui-capture JSON files")
    ap.add_argument("-o", "--out", default="capture.html")
    ap.add_argument("--cursor-color", default=None,
                    help="R,G,B to test instead of the pinned accent")
    a = ap.parse_args()

    accent, tol, source = load_accent()
    if a.cursor_color:
        try:
            accent = [int(v) for v in a.cursor_color.split(",")]
            if len(accent) != 3:
                raise ValueError
        except ValueError:
            print("--cursor-color wants R,G,B", file=sys.stderr)
            return 2
        source = "--cursor-color"

    sections = []
    for path in a.captures:
        try:
            cap = load_capture(path)
        except (OSError, ValueError) as e:
            print("%s: %s" % (path, e), file=sys.stderr)
            return 2
        sections.append(render_one(cap, accent, tol, os.path.basename(path)))

    doc = ("<!doctype html><meta charset='utf-8'>"
           "<title>M8 capture &mdash; %s</title><style>%s</style>"
           "<h1>M8 screen capture</h1>"
           "<p class='sub'>Accent <b>%s</b> &plusmn;%d, from %s. "
           "Outlined cells are what the driver believes is the cursor.</p>"
           "<div class='wrap'>%s</div>"
           % (html.escape(", ".join(os.path.basename(p) for p in a.captures)),
              CSS, html.escape(str(accent)), tol, html.escape(source),
              "\n".join(sections)))

    with open(a.out, "w", encoding="utf-8") as f:
        f.write(doc)
    print("wrote %s (%d capture%s)" % (a.out, len(a.captures),
                                       "" if len(a.captures) == 1 else "s"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
