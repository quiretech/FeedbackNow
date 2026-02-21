#!/usr/bin/env python3
"""
eui_collision_check.py

Reads eui_registry.csv and reports:
- Duplicate dev_eui
- Duplicate join_eui
- Duplicate app_key
- Cross-field collisions (dev_eui == join_eui, etc.)
- Duplicate asset_id
"""

import csv
import sys
import re
from pathlib import Path
from collections import defaultdict


def normalize_hex(s: str) -> str:
    return re.sub(r"[^0-9A-Fa-f]", "", (s or "")).upper()


def load_csv(path: Path):
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        required = {"asset_id", "dev_eui", "join_eui", "app_key"}
        if not reader.fieldnames or not required <= set(reader.fieldnames):
            raise SystemExit(
                f"CSV must contain headers: {', '.join(required)}"
            )

        for line_num, row in enumerate(reader, start=2):
            rows.append({
                "line": line_num,
                "asset_id": (row.get("asset_id") or "").strip(),
                "dev_eui": normalize_hex(row.get("dev_eui")),
                "join_eui": normalize_hex(row.get("join_eui")),
                "app_key": normalize_hex(row.get("app_key")),
            })
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


def main():
    csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("eui_registry.csv")

    if not csv_path.exists():
        print(f"ERROR: CSV not found: {csv_path}")
        sys.exit(1)

    rows = load_csv(csv_path)

    print(f"\nLoaded {len(rows)} rows from {csv_path}\n")

    problems_found = False

    for field in ["asset_id", "dev_eui", "join_eui", "app_key"]:
        duplicates = check_duplicates(rows, field)
        if duplicates:
            problems_found = True
            print(f"❌ Duplicate {field} detected:")
            for value, entries in duplicates.items():
                print(f"  Value: {value}")
                for e in entries:
                    print(f"    Line {e['line']} (asset_id={e['asset_id']})")
            print()

    cross = check_cross_collisions(rows)
    if cross:
        problems_found = True
        print("❌ Cross-field collisions detected:")
        for a, b, reason in cross:
            print(
                f"  Line {a['line']} (asset_id={a['asset_id']}) "
                f"and Line {b['line']} (asset_id={b['asset_id']}): {reason}"
            )
        print()

    if not problems_found:
        print("✅ No collisions detected.")
    else:
        print("⚠️  Collisions found. Fix before onboarding.")
        sys.exit(2)


if __name__ == "__main__":
    main()