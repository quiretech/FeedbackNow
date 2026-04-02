#!/usr/bin/env python3
"""
eui_collision_check.py

Reads eui_registry.csv and eui_registry_eu868.csv and reports:
- Duplicate dev_eui
- Duplicate join_eui
- Duplicate app_key
- Cross-field collisions (dev_eui == join_eui, etc.)
- Duplicate asset_id
- Collisions across both files
"""

import csv
import sys
import re
from pathlib import Path
from collections import defaultdict

def normalize_hex(s: str) -> str:
    return re.sub(r"[^0-9A-Fa-f]", "", (s or "")).upper()

def load_csv(path: Path, filetag: str = None):
    """
    Load a CSV, returning rows as dicts with a 'line' number and optionally a 'source' field
    """
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        required = {"asset_id", "dev_eui", "join_eui", "app_key"}
        if not reader.fieldnames or not required <= set(reader.fieldnames):
            raise SystemExit(
                f"CSV must contain headers: {', '.join(required)}"
            )
        for line_num, row in enumerate(reader, start=2):
            entry = {
                "line": line_num,
                "asset_id": (row.get("asset_id") or "").strip(),
                "dev_eui": normalize_hex(row.get("dev_eui")),
                "join_eui": normalize_hex(row.get("join_eui")),
                "app_key": normalize_hex(row.get("app_key")),
            }
            if filetag:
                entry["source"] = filetag
            rows.append(entry)
    return rows

def check_duplicates(rows, field):
    seen = defaultdict(list)
    for r in rows:
        seen[r[field]].append(r)
    duplicates = {k: v for k, v in seen.items() if k and len(v) > 1}
    return duplicates

def check_cross_collisions(rows):
    collisions = []
    for i, a in enumerate(rows):
        for b in rows[i+1:]:
            if a["dev_eui"] and a["dev_eui"] == b["join_eui"]:
                collisions.append(
                    (a, b, "dev_eui == join_eui")
                )
            if a["join_eui"] and a["join_eui"] == b["dev_eui"]:
                collisions.append(
                    (a, b, "join_eui == dev_eui")
                )
    return collisions

def format_entry(e):
    """Format an entry for printing, including file source and line if present"""
    src = f"{e.get('source', 'main')}:L{e['line']}"
    return f"{src} (asset_id={e['asset_id']})"

def main():
    # Default files to check
    main_csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("eui_registry.csv")
    eu868_csv_path = Path("eui_registry_eu868.csv")
    problems_found = False

    files_loaded = []
    sources = []

    # Always include main file
    if not main_csv_path.exists():
        print(f"ERROR: CSV not found: {main_csv_path}")
        sys.exit(1)
    files_loaded.append(load_csv(main_csv_path, "main"))
    sources.append(str(main_csv_path))

    # Optionally include eui_registry_eu868.csv if present
    if eu868_csv_path.exists() and eu868_csv_path.resolve() != main_csv_path.resolve():
        files_loaded.append(load_csv(eu868_csv_path, "eu868"))
        sources.append(str(eu868_csv_path))

    # Flatten all to a single list with source tags
    all_rows = [row for rows in files_loaded for row in rows]

    print(f"\nLoaded {sum(map(len, files_loaded))} rows from: {', '.join(sources)}\n")

    # Per-file duplicate detection (for user clarity, also report source file)
    for field in ["asset_id", "dev_eui", "join_eui", "app_key"]:
        duplicates = check_duplicates(all_rows, field)
        if duplicates:
            problems_found = True
            print(f"❌ Duplicate {field} detected across all input files:")
            for value, entries in duplicates.items():
                print(f"  Value: {value}")
                for e in entries:
                    print(f"    {format_entry(e)}")
            print()

    # Cross-field collisions across all entries
    cross = check_cross_collisions(all_rows)
    if cross:
        problems_found = True
        print("❌ Cross-field collisions detected (across all input files):")
        for a, b, reason in cross:
            print(
                f"  {format_entry(a)} and {format_entry(b)}: {reason}"
            )
        print()

    if not problems_found:
        print("✅ No collisions detected.")
    else:
        print("⚠️  Collisions found. Fix before onboarding.")
        sys.exit(2)

if __name__ == "__main__":
    main()