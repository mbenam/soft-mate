#!/usr/bin/env python3
"""
check_doc_flags.py — Verifies that all CLI flags in src/tools/main_*.cpp
are documented in their corresponding docs/tools/m8_*.md documentation files,
and vice versa.
"""

import sys
import re
from pathlib import Path

TOOLS = [
    ("analyze",     "src/tools/main_analyze.cpp",     "docs/tools/m8_analyze.md"),
    ("capture",     "src/tools/main_capture.cpp",     "docs/tools/m8_capture.md"),
    ("composesong", "src/tools/main_composesong.cpp", "docs/tools/m8_composesong.md"),
    ("diffcheck",   "src/tools/main_diffcheck.cpp",   "docs/tools/m8_diffcheck.md"),
    ("crawl",       "src/tools/main_crawl.cpp",       "docs/tools/m8_crawl.md"),
    ("editwatch",   "src/tools/main_editwatch.cpp",   "docs/tools/m8_editwatch.md"),
    ("livecheck",   "src/tools/main_livecheck.cpp",   "docs/tools/m8_livecheck.md"),
    ("makeprobe",   "src/tools/main_makeprobe.cpp",   "docs/tools/m8_makeprobe.md"),
    ("makesong",    "src/tools/main_makesong.cpp",    "docs/tools/m8_makesong.md"),
    ("nav",         "src/tools/main_nav.cpp",         "docs/tools/m8_nav.md"),
    ("watchcapture","src/tools/main_watchcapture.cpp","docs/tools/m8_watchcapture.md"),
    ("render",      "src/tools/main_render.cpp",      "docs/tools/m8_render.md"),
    ("sweep",       "src/tools/main_sweep.cpp",       "docs/tools/m8_sweep.md"),
    ("spectrum",    "src/tools/main_spectrum.cpp",    "docs/tools/m8_spectrum.md"),
]

def check_tool(name: str, cpp_path: Path, md_path: Path) -> bool:
    if not cpp_path.exists():
        print(f"[{name}] ERROR: C++ file missing: {cpp_path}")
        return False
    if not md_path.exists():
        print(f"[{name}] ERROR: MD file missing: {md_path}")
        return False

    cpp_content = cpp_path.read_text(encoding="utf-8")
    md_content = md_path.read_text(encoding="utf-8")

    # Match string literals starting with "--" in C++ source, excluding pure dash strings
    raw_code_flags = re.findall(r'"(--[a-zA-Z0-9_-]+)"', cpp_content)
    code_flags = set(f for f in raw_code_flags if not set(f) == {'-'})

    # Find "## CLI flags" section
    cli_match = re.search(r'## CLI flags.*?(?=\n## |\Z)', md_content, re.DOTALL)
    search_text = cli_match.group(0) if cli_match else md_content

    # Extract flags from first column of markdown tables within search_text
    doc_flags = set()
    for line in search_text.splitlines():
        line = line.strip()
        if line.startswith("|") and line.endswith("|"):
            parts = line.split("|")
            if len(parts) >= 3:
                first_col = parts[1]
                for flag in re.findall(r'--[a-zA-Z0-9_-]+', first_col):
                    if not set(flag) == {'-'}:
                        doc_flags.add(flag)

    # Fallback to general regex in CLI flags section if no table flags were extracted
    if not doc_flags:
        for flag in re.findall(r'--[a-zA-Z0-9_-]+', search_text):
            if not set(flag) == {'-'}:
                doc_flags.add(flag)

    missing_in_docs = code_flags - doc_flags
    extra_in_docs = doc_flags - code_flags

    ok = True
    if missing_in_docs or extra_in_docs:
        print(f"[{name}] DIVERGENCE DETECTED in {md_path}:")
        if missing_in_docs:
            print(f"  Flags in code but missing in docs: {sorted(missing_in_docs)}")
        if extra_in_docs:
            print(f"  Flags in docs but missing in code: {sorted(extra_in_docs)}")
        ok = False
    else:
        print(f"[{name}] OK ({len(code_flags)} flags checked)")

    return ok

def main():
    root = Path(__file__).resolve().parent.parent
    all_ok = True
    print("Checking tool CLI flag documentation consistency...")
    for name, cpp_rel, md_rel in TOOLS:
        cpp_path = root / cpp_rel
        md_path = root / md_rel
        if not check_tool(name, cpp_path, md_path):
            all_ok = False

    if all_ok:
        print("\nSUCCESS: All tool CLI flags match documentation perfectly.")
        sys.exit(0)
    else:
        print("\nFAILURE: Flag documentation divergence found. Please update docs.")
        sys.exit(1)

if __name__ == "__main__":
    main()
