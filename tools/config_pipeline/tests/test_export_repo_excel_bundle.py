import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "export.py"
REPO_ROOT = Path(__file__).resolve().parents[3]


class ExportRepoExcelBundleTests(unittest.TestCase):
    def test_exports_complete_bundle_from_repo_config_excel(self):
        source_dir = REPO_ROOT / "config" / "excel"
        with tempfile.TemporaryDirectory() as out_tmp:
            out_dir = Path(out_tmp)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT_PATH),
                    "--source-dir",
                    str(source_dir),
                    "--out-dir",
                    str(out_dir),
                    "--generated-at",
                    "2026-03-07T00:00:00Z",
                ],
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["bundle_type"], "gameplay")
            self.assertEqual(
                [artifact["name"] for artifact in manifest["artifacts"]],
                ["drops", "gates", "items", "maps", "monster_spawns", "npcs", "shops", "skills"],
            )
            for artifact in manifest["artifacts"]:
                self.assertTrue((out_dir / artifact["file"]).exists(), artifact["file"])


if __name__ == "__main__":
    unittest.main()
