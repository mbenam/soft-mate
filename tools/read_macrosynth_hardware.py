#!/usr/bin/env python3
"""
Dump the MacroSynth screen from the real M8 on COM4
and cycle through all SHAPE, FILTER, and LIM values to capture the exact hardware strings.
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
        
        # Check current screen
        dump = d.send("DUMP")
        print("\n--- Current Instrument Screen Dump ---")
        lines = dump.get("lines", [])
        for line in lines:
            print(line)
        
        # Read shapes by setting SHAPE from 0 to 43 (0x00 to 0x2B)
        print("\n--- Reading Shapes 0x00 to 0x2B ---")
        shapes = []
        for s in range(44):
            hex_val = f"{s:02X}"
            try:
                # We can set SHAPE
                d.send("SET", field="SHAPE", value=hex_val)
                # Read row
                r = d.send("READ", field="SHAPE")
                row = r.get("row", "")
                val = r.get("value", "")
                shapes.append((s, hex_val, val, row))
                print(f"Shape {hex_val}: val='{val}' row='{row}'")
            except Exception as e:
                print(f"Error on shape {hex_val}: {e}")
                break

        # Read filters 0 to 7
        print("\n--- Reading Filters 0x00 to 0x07 ---")
        filters = []
        for f in range(8):
            hex_val = f"{f:02X}"
            try:
                d.send("SET", field="FILTER", value=hex_val)
                r = d.send("READ", field="FILTER")
                row = r.get("row", "")
                val = r.get("value", "")
                filters.append((f, hex_val, val, row))
                print(f"Filter {hex_val}: val='{val}' row='{row}'")
            except Exception as e:
                print(f"Error on filter {hex_val}: {e}")
                break

        # Read LIM 0 to 8
        print("\n--- Reading LIM 0x00 to 0x08 ---")
        lims = []
        for l in range(9):
            hex_val = f"{l:02X}"
            try:
                d.send("SET", field="LIM", value=hex_val)
                r = d.send("READ", field="LIM")
                row = r.get("row", "")
                val = r.get("value", "")
                lims.append((l, hex_val, val, row))
                print(f"LIM {hex_val}: val='{val}' row='{row}'")
            except Exception as e:
                print(f"Error on LIM {hex_val}: {e}")
                break

        # Restore defaults: shape 00, filter 00, lim 00
        d.send("SET", field="SHAPE", value="00")
        d.send("SET", field="FILTER", value="00")
        d.send("SET", field="LIM", value="00")

        # Save to artifacts/macrosynth_hardware_probe.json
        out_data = {
            "dump": lines,
            "shapes": shapes,
            "filters": filters,
            "lims": lims
        }
        os.makedirs("artifacts", exist_ok=True)
        with open("artifacts/macrosynth_hardware_probe.json", "w", encoding="utf-8") as f:
            json.dump(out_data, f, indent=2)
        print("\nSaved to artifacts/macrosynth_hardware_probe.json")

if __name__ == "__main__":
    main()
