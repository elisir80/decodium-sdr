#!/usr/bin/env python3
"""Exercise both staging layouts and missing/empty attribution files."""
import pathlib
import os
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check-onair-terms.sh"
BUILD = pathlib.Path(os.environ.get("DSDR_TEST_BUILD_DIR", ROOT / "build"))
SHELL = os.environ.get("DSDR_TEST_SHELL", "sh")
FILES = ("OnAir/ADDITIONAL-TERMS.md", "OnAir/NOTICE.md", "OnAir/THIRD_PARTY.md",
         "licenses/LICENSE", "licenses/THIRD_PARTY_LICENSES")


class PackageTermsTest(unittest.TestCase):
    def test_layouts_and_missing_files(self):
        with tempfile.TemporaryDirectory(dir=BUILD) as tmp:
            prefix = pathlib.Path(tmp)
            for platform in ("macos", "linux", "windows"):
                base = (prefix / "decodium-sdr.app/Contents/Resources" if platform == "macos"
                        else prefix / "share/decodium-sdr")
                for name in FILES:
                    path = base / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("fixture\n", encoding="utf-8")
                def check():
                    return subprocess.run([SHELL, str(SCRIPT), str(prefix), platform],
                                          capture_output=True, text=True)
                self.assertEqual(check().returncode, 0, platform)
                for name in FILES:
                    path = base / name
                    path.write_text("", encoding="utf-8")
                    result = check()
                    self.assertNotEqual(result.returncode, 0, (platform, name))
                    self.assertIn(name, result.stderr)
                    path.unlink()
                    self.assertNotEqual(check().returncode, 0, (platform, name))
                    path.write_text("fixture\n", encoding="utf-8")
            # A typo must not silently select the non-macOS layout, even if
            # that layout contains all required files.
            bad = subprocess.run([SHELL, str(SCRIPT), str(prefix), "linxu"],
                                 capture_output=True, text=True)
            self.assertEqual(bad.returncode, 2)
            self.assertIn("Unsupported platform", bad.stderr)


if __name__ == "__main__":
    unittest.main()
