#!/usr/bin/env python3
import importlib.util
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("onair_mode_inventory", ROOT / "scripts/onair_mode_inventory.py")
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)


class InventoryTest(unittest.TestCase):
    def test_valid_and_wrong_counts(self):
        good = "**2 modes.**\n## Example (2)\n- **One** - x\n- **Two** - y\n"
        self.assertEqual(inventory.parse_modes(good)["declared_modes"], 2)
        with self.assertRaises(ValueError):
            inventory.parse_modes(good.replace("Example (2)", "Example (3)"))
        with self.assertRaises(ValueError):
            inventory.parse_modes(good.replace("**2 modes.**", "**3 modes.**"))

    def test_snapshot_is_complete_without_claiming_support(self):
        snapshot = json.loads((ROOT / "docs/onair_mode_inventory_v3.9.0.json").read_text())
        self.assertEqual(snapshot["declared_modes"], 333)
        self.assertEqual(sum(len(g["modes"]) for g in snapshot["groups"]), 333)
        self.assertTrue(all(m["coverage"] == "not_assessed" for g in snapshot["groups"] for m in g["modes"]))


if __name__ == "__main__":
    unittest.main()
