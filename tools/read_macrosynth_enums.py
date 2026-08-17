#!/usr/bin/env python3
"""
Step through enums on MacroSynth screen using EDIT+RIGHT (0x05) to read all values directly.
"""
import sys
import os
import json
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "m8drv"))
from m8drv import M8Driver

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    print(f"Connecting to {port}...")
    with M8Driver(port=port) as d:
        print("Connected!")
        d.home()
        d.goto("INSTRUMENT")
        
        # 1. Capture initial screen dump
        dump = d.send("DUMP")
        print("\n--- Initial Instrument Screen Dump ---")
        for line in dump.get("lines", []):
            print(line)

        # 2. Navigate to SHAPE
        print("\n--- Reading Shapes (0 to 43) ---")
        d.cursor("SHAPE")
        
        # Reset shape to 00 by stepping down 50 times with 0x81 (EDIT+LEFT)
        for _ in range(50):
            d.press(0x81)
            time.sleep(0.02)
        
        shapes = []
        for i in range(44):
            # Read state
            st = d.state()
            cv = st.get("cursor_value", "")
            # Also get row text
            rows = st.get("rows", [])
            shape_row = next((r.get("text", "") for r in rows if "SHAPE" in r.get("text", "")), "")
            print(f"Shape index {i:02X}: cursor_val='{cv}', row='{shape_row}'")
            shapes.append({"index": i, "hex": f"{i:02X}", "cursor_value": cv, "row": shape_row})
            
            # Step to next shape
            if i < 43:
                d.press(0x05)
                time.sleep(0.05)

        # Reset shape to 00
        for _ in range(50):
            d.press(0x81)
            time.sleep(0.02)

        # 3. Read FILTERS
        print("\n--- Reading Filters (0 to 7) ---")
        d.cursor("FILTER")
        for _ in range(10):
            d.press(0x81)
            time.sleep(0.02)
            
        filters = []
        for i in range(8):
            st = d.state()
            cv = st.get("cursor_value", "")
            rows = st.get("rows", [])
            filt_row = next((r.get("text", "") for r in rows if "FILTER" in r.get("text", "")), "")
            print(f"Filter index {i:02X}: cursor_val='{cv}', row='{filt_row}'")
            filters.append({"index": i, "hex": f"{i:02X}", "cursor_value": cv, "row": filt_row})
            if i < 7:
                d.press(0x05)
                time.sleep(0.05)

        for _ in range(10):
            d.press(0x81)
            time.sleep(0.02)

        # 4. Read LIM
        print("\n--- Reading LIM (0 to 8) ---")
        d.cursor("LIM")
        for _ in range(12):
            d.press(0x81)
            time.sleep(0.02)

        lims = []
        for i in range(9):
            st = d.state()
            cv = st.get("cursor_value", "")
            rows = st.get("rows", [])
            lim_row = next((r.get("text", "") for r in rows if "LIM" in r.get("text", "")), "")
            print(f"LIM index {i:02X}: cursor_val='{cv}', row='{lim_row}'")
            lims.append({"index": i, "hex": f"{i:02X}", "cursor_value": cv, "row": lim_row})
            if i < 8:
                d.press(0x05)
                time.sleep(0.05)

        for _ in range(12):
            d.press(0x81)
            time.sleep(0.02)

        out_data = {
            "shapes": shapes,
            "filters": filters,
            "lims": lims
        }
        os.makedirs("artifacts", exist_ok=True)
        with open("artifacts/macrosynth_enums.json", "w", encoding="utf-8") as f:
            json.dump(out_data, f, indent=2)
        print("\nSaved all enums to artifacts/macrosynth_enums.json")

if __name__ == "__main__":
    main()
