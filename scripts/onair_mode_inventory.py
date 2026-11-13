#!/usr/bin/env python3
"""Create a reproducible, honest inventory from a pinned OnAir MODES.md."""
import argparse
import json
import re
from pathlib import Path

GROUP = re.compile(r"^## (.+) \((\d+)\)$")
MODE = re.compile(r"^- \*\*(.+?)\*\* - ")
TOTAL = re.compile(r"^\*\*(\d+) modes\.\*\*$")


def parse_modes(text):
    total = None
    groups = []
    current = None
    for line in text.splitlines():
        match = TOTAL.match(line)
        if match:
            total = int(match.group(1))
        match = GROUP.match(line)
        if match:
            current = {"name": match.group(1), "expected": int(match.group(2)), "modes": []}
            groups.append(current)
            continue
        match = MODE.match(line)
        if match:
            if current is None:
                raise ValueError("Mode found before a group")
            current["modes"].append({"name": match.group(1), "coverage": "not_assessed"})
    if total is None or not groups:
        raise ValueError("Missing declared total or groups")
    if any(len(group["modes"]) != group["expected"] for group in groups):
        raise ValueError("A group does not match its declared count")
    if sum(len(group["modes"]) for group in groups) != total:
        raise ValueError("Inventory does not match declared total")
    return {"source": "https://github.com/adswill/OnAir/blob/v3.9.0/MODES.md",
            "source_commit": "15a2907f02f7486fc64b8e439a1645eb7373171f",
            "declared_modes": total, "groups": groups}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    inventory = parse_modes(args.source.read_text(encoding="utf-8"))
    args.output.write_text(json.dumps(inventory, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
