#!/usr/bin/env python3
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "m8drv"))
from m8drv import M8Driver

with M8Driver("COM4") as d:
    d.goto("INSTRUMENT")
    st = d.state()
    for r in st.get("rows", []):
        text = r.get("text", "")
        print(f"Row {r.get('y'):2d}: '{text}'")
