#!/usr/bin/env python3
"""Render two UiCapture JSON files as a 40-column side-by-side text grid.

Usage:
    python tools/side_by_side.py <left.json> <right.json> [--out <file.txt>]

Each side gets 40 columns. The output is 80 columns wide plus a separator.
Rows are numbered. Cells with a non-space character are rendered; bg-only
cells show as '.' to make them visible without noise.
"""
import json
import sys
import argparse


def load_grid(path, max_rows=24, max_cols=40):
    with open(path) as f:
        data = json.load(f)
    grid = {}
    for c in data.get("cells", []):
        r, col = c["row"], c["col"]
        if r < max_rows and col < max_cols:
            grid[(r, col)] = c
    return grid


def render_side(grid, max_rows=24, max_cols=40):
    lines = []
    for r in range(max_rows):
        line = []
        for c in range(max_cols):
            cell = grid.get((r, c))
            if cell:
                ch = cell.get("ch", " ")
                if ch != " ":
                    line.append(ch)
                else:
                    # bg-only cell: show as dot
                    line.append(".")
            else:
                line.append(" ")
        lines.append("".join(line).rstrip())
    return lines


def main():
    parser = argparse.ArgumentParser(description="Side-by-side UiCapture grid renderer")
    parser.add_argument("left", help="Left (device) UiCapture JSON")
    parser.add_argument("right", help="Right (clone) UiCapture JSON")
    parser.add_argument("--out", help="Output file (default: stdout)")
    args = parser.parse_args()

    left_grid = load_grid(args.left)
    right_grid = load_grid(args.right)

    left_lines = render_side(left_grid)
    right_lines = render_side(right_grid)

    max_rows = max(len(left_lines), len(right_lines))
    left_lines += [""] * (max_rows - len(left_lines))
    right_lines += [""] * (max_rows - len(right_lines))

    sep = " | "
    output_lines = []
    output_lines.append(f"{'LEFT (device)':40s}{sep}{'RIGHT (clone)':40s}")
    output_lines.append(f"{'-'*40}{sep}{'-'*40}")
    for i, (l, r) in enumerate(zip(left_lines, right_lines)):
        output_lines.append(f"{l:40s}{sep}{r:40s}")

    result = "\n".join(output_lines) + "\n"

    if args.out:
        with open(args.out, "w") as f:
            f.write(result)
        print(f"wrote {args.out}")
    else:
        print(result)


if __name__ == "__main__":
    main()
