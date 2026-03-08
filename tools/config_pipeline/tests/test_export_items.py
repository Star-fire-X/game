import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from openpyxl import Workbook


SCRIPT_PATH = (
    Path(__file__).resolve().parents[1] / "export.py"
)


class ExportItemsTests(unittest.TestCase):
    def _write_items_workbook(self, source_dir: Path, headers, rows, title: str = "items") -> Path:
        workbook = Workbook()
        sheet = workbook.active
        sheet.title = title
        sheet.append(headers)
        for row in rows:
            sheet.append(row)
        path = source_dir / "items.xlsx"
        workbook.save(path)
        return path

    def _artifact_by_name(self, manifest, name: str):
        for artifact in manifest["artifacts"]:
            if artifact["name"] == name:
                return artifact
        self.fail(f"artifact {name} not found in manifest")

    def _run_export(self, source_dir: Path, out_dir: Path):
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT_PATH),
                "--source-dir",
                str(source_dir),
                "--out-dir",
                str(out_dir),
                "--tables",
                "items",
                "--generated-at",
                "2026-03-07T00:00:00Z",
            ],
            capture_output=True,
            text=True,
        )

    def test_exports_items_json_and_manifest(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_items_workbook(
                source_dir,
                ["id", "name", "std_mode", "price", "stackable", "stack_limit"],
                [
                    [1002, "Town Scroll", 3, 200, True, 10],
                    [1001, "Small Heal", 0, 100, True, 20],
                ],
            )

            result = self._run_export(source_dir, out_dir)

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            items_path = out_dir / "items.json"
            manifest_path = out_dir / "manifest.json"
            self.assertTrue(items_path.exists())
            self.assertTrue(manifest_path.exists())

            items_text = items_path.read_text(encoding="utf-8")
            items_json = json.loads(items_text)
            self.assertEqual(
                [item["id"] for item in items_json["items"]],
                [1001, 1002],
            )
            self.assertEqual(items_json["items"][0]["name"], "Small Heal")
            self.assertEqual(items_json["items"][1]["std_mode"], 3)
            self.assertEqual(items_json["items"][0]["stack_limit"], 20)
            self.assertEqual(items_json["items"][0]["need_level"], 0)

            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["bundle_type"], "gameplay")
            self.assertEqual(manifest["schema_version"], 1)
            self.assertEqual(manifest["generated_at"], "2026-03-07T00:00:00Z")
            self.assertIsInstance(manifest["artifacts"], list)
            self.assertEqual(len(manifest["artifacts"]), 1)
            self.assertNotIn("generation", manifest)
            artifact = self._artifact_by_name(manifest, "items")
            self.assertEqual(artifact["file"], "items.json")
            self.assertEqual(artifact["row_count"], 2)
            self.assertEqual(
                artifact["hash"],
                hashlib.sha256(items_text.encode("utf-8")).hexdigest(),
            )

    def test_rejects_missing_required_headers(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_items_workbook(
                source_dir,
                ["name", "price"],
                [["Small Heal", 100]],
            )

            result = self._run_export(source_dir, out_dir)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing required columns", result.stderr.lower())
            self.assertFalse((out_dir / "items.json").exists())
            self.assertFalse((out_dir / "manifest.json").exists())

    def test_rejects_mismatched_sheet_name(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_items_workbook(
                source_dir,
                ["id", "name"],
                [[1001, "Small Heal"]],
                title="item_table",
            )

            result = self._run_export(source_dir, out_dir)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("sheet", result.stderr.lower())
            self.assertIn("items", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()
