#!/usr/bin/env python3
"""
tools/m8s_diff.py — Byte-diff harness for M8 .m8s files (P2 step).

Compares a generated .m8s file (from m8_makeprobe) against a golden .m8s file,
grouping contiguous byte differences and filtering out documented benign fields.

Usage:
  python tools/m8s_diff.py <generated.m8s> <golden.m8s>
"""

import sys
import os

# Known benign byte ranges that differ between device-saved files and generated probes.
# Each entry is (start_offset, end_offset_exclusive, description).
BENIGN_RANGES = [
    (0, 4, "File magic / header version prefix"),
    (0x0E, 0x8E, "Project directory path string (128 bytes)"),
    (0x93, 0x9F, "Project name string (12 bytes)"),
]

def is_benign(offset):
    for start, end, _ in BENIGN_RANGES:
        if start <= offset < end:
            return True
    return False

def get_benign_reason(offset):
    for start, end, desc in BENIGN_RANGES:
        if start <= offset < end:
            return desc
    return None

def diff_m8s(gen_path, golden_path):
    if not os.path.exists(gen_path):
        print(f"Error: Generated file not found: {gen_path}")
        return 1
    if not os.path.exists(golden_path):
        print(f"Error: Golden file not found: {golden_path}")
        return 1

    with open(gen_path, "rb") as f:
        gen_bytes = f.read()
    with open(golden_path, "rb") as f:
        golden_bytes = f.read()

    max_len = max(len(gen_bytes), len(golden_bytes))
    min_len = min(len(gen_bytes), len(golden_bytes))

    if len(gen_bytes) != len(golden_bytes):
        print(f"File size mismatch: generated {len(gen_bytes)} bytes vs golden {len(golden_bytes)} bytes")

    diffs = []
    i = 0
    while i < max_len:
        g = gen_bytes[i] if i < len(gen_bytes) else None
        ref = golden_bytes[i] if i < len(golden_bytes) else None

        if g != ref:
            start_off = i
            gen_buf = []
            ref_buf = []
            while i < max_len:
                g_byte = gen_bytes[i] if i < len(gen_bytes) else None
                ref_byte = golden_bytes[i] if i < len(golden_bytes) else None
                if g_byte == ref_byte:
                    break
                gen_buf.append(g_byte)
                ref_buf.append(ref_byte)
                i += 1
            length = i - start_off
            diffs.append((start_off, length, gen_buf, ref_buf))
        else:
            i += 1

    suspicious_count = 0
    print(f"\nComparing '{gen_path}' vs '{golden_path}':")
    print(f"{'Offset (Dec)':<14} {'Offset (Hex)':<14} {'Len':<6} {'Gen Bytes':<20} {'Golden Bytes':<20} {'Status / Reason'}")
    print("-" * 95)

    for start_off, length, gen_buf, ref_buf in diffs:
        benign = all(is_benign(start_off + k) for k in range(length))
        reason = get_benign_reason(start_off) if benign else "SUSPICIOUS DIFFERENCE"
        if not benign:
            suspicious_count += 1

        gen_hex = " ".join(f"{b:02X}" if b is not None else "--" for b in gen_buf[:8])
        if len(gen_buf) > 8:
            gen_hex += "..."
        ref_hex = " ".join(f"{b:02X}" if b is not None else "--" for b in ref_buf[:8])
        if len(ref_buf) > 8:
            ref_hex += "..."

        status_tag = "[BENIGN]" if benign else "[SUSPICIOUS]"
        print(f"{start_off:<14} {hex(start_off):<14} {length:<6} {gen_hex:<20} {ref_hex:<20} {status_tag} {reason}")

    print("-" * 95)
    print(f"Total differing regions: {len(diffs)} ({suspicious_count} suspicious, {len(diffs) - suspicious_count} benign)")
    return 0 if suspicious_count == 0 else 2

def dump_fields(m8s_path):
    if not os.path.exists(m8s_path):
        print(f"Error: File not found: {m8s_path}")
        return 1
    import subprocess
    cmd = ["build/Release/m8_makeprobe.exe", "--inspect", m8s_path]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Error running inspect: {res.stderr}")
        return res.returncode
    print(res.stdout)
    return 0

if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--dump-fields":
        if len(sys.argv) < 3:
            print("Usage: python tools/m8s_diff.py --dump-fields <file.m8s>")
            sys.exit(1)
        sys.exit(dump_fields(sys.argv[2]))
    if len(sys.argv) < 3:
        print("Usage: python tools/m8s_diff.py <generated.m8s> <golden.m8s>")
        sys.exit(1)
    sys.exit(diff_m8s(sys.argv[1], sys.argv[2]))
