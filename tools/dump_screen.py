#!/usr/bin/env python3
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "m8drv"))
from m8drv import M8Driver

target = sys.argv[1] if len(sys.argv) > 1 else "GROOVE"
port = sys.argv[2] if len(sys.argv) > 2 else "COM4"

with M8Driver(port) as d:
    d.goto(target)
    st = d.state()
    print(f"Screen: {st.get('screen')} at col={st.get('cursor_col')}, row={st.get('cursor_row')}")
    for r in st.get("rows", []):
        text = r.get("text", "")
        print(f"Row {r.get('y'):2d}: '{text}'")

