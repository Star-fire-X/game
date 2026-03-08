import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from openpyxl import Workbook


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "export.py"


class ExportSkillsTests(unittest.TestCase):
    def _write_workbook(self, path: Path, headers, rows, title: str):
        workbook = Workbook()
        sheet = workbook.active
        sheet.title = title
        sheet.append(headers)
        for row in rows:
            sheet.append(row)
        path.parent.mkdir(parents=True, exist_ok=True)
        workbook.save(path)

    def _artifact_by_name(self, manifest, name: str):
        for artifact in manifest["artifacts"]:
            if artifact["name"] == name:
                return artifact
        self.fail(f"artifact {name} not found in manifest")

    def _run_export(self, source_dir: Path, out_dir: Path, tables):
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT_PATH),
                "--source-dir",
                str(source_dir),
                "--out-dir",
                str(out_dir),
                "--tables",
                *tables,
                "--generated-at",
                "2026-03-07T00:00:00Z",
            ],
            capture_output=True,
            text=True,
        )

    def test_exports_skills_json_and_manifest(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "skills.xlsx",
                [
                    "id",
                    "name",
                    "required_class",
                    "required_level",
                    "skill_type",
                    "target_type",
                    "mp_cost",
                    "cooldown_ms",
                ],
                [
                    [11, "Lightning", "MAGE", 19, "MAGICAL", "SINGLE_ENEMY", 14, 1000],
                    [3, "Attack Training", "WARRIOR", 7, "PHYSICAL", "SINGLE_ENEMY", 3, 800],
                ],
                "skills",
            )

            result = self._run_export(source_dir, out_dir, ["skills"])

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            skills_path = out_dir / "skills.json"
            manifest_path = out_dir / "manifest.json"
            self.assertTrue(skills_path.exists())
            self.assertTrue(manifest_path.exists())

            skills_text = skills_path.read_text(encoding="utf-8")
            skills_json = json.loads(skills_text)
            self.assertEqual([skill["id"] for skill in skills_json["skills"]], [3, 11])
            self.assertEqual(skills_json["skills"][0]["name"], "Attack Training")
            self.assertEqual(skills_json["skills"][0]["description"], "")
            self.assertEqual(skills_json["skills"][0]["max_level"], 3)
            self.assertEqual(skills_json["skills"][0]["train_level_req"], [0, 0, 0, 0])
            self.assertEqual(skills_json["skills"][0]["train_points_req"], [0, 0, 0, 0])
            self.assertFalse(skills_json["skills"][0]["is_universal"])
            self.assertEqual(skills_json["skills"][1]["cooldown_ms"], 1000)

            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(manifest["bundle_type"], "gameplay")
            self.assertEqual(manifest["schema_version"], 1)
            self.assertEqual(manifest["generated_at"], "2026-03-07T00:00:00Z")
            self.assertNotIn("generation", manifest)
            artifact = self._artifact_by_name(manifest, "skills")
            self.assertEqual(artifact["file"], "skills.json")
            self.assertEqual(artifact["row_count"], 2)
            self.assertEqual(
                artifact["hash"],
                hashlib.sha256(skills_text.encode("utf-8")).hexdigest(),
            )

    def test_rejects_missing_required_skill_headers(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "skills.xlsx",
                ["id", "name", "required_level", "skill_type", "target_type"],
                [[3, "Attack Training", 7, "PHYSICAL", "SINGLE_ENEMY"]],
                "skills",
            )

            result = self._run_export(source_dir, out_dir, ["skills"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing required columns", result.stderr.lower())
            self.assertFalse((out_dir / "skills.json").exists())
            self.assertFalse((out_dir / "manifest.json").exists())

    def test_rejects_non_canonical_skill_alias_headers(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "skills.xlsx",
                ["id", "name", "profession", "required_level", "skill_type", "target_type"],
                [[3, "Attack Training", "WARRIOR", 7, "PHYSICAL", "SINGLE_ENEMY"]],
                "skills",
            )

            result = self._run_export(source_dir, out_dir, ["skills"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing required columns", result.stderr.lower())

    def test_rejects_duplicate_skill_ids(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "skills.xlsx",
                ["id", "name", "required_class", "required_level", "skill_type", "target_type"],
                [
                    [3, "Attack Training", "WARRIOR", 7, "PHYSICAL", "SINGLE_ENEMY"],
                    [3, "Another Attack", "WARRIOR", 8, "PHYSICAL", "SINGLE_ENEMY"],
                ],
                "skills",
            )

            result = self._run_export(source_dir, out_dir, ["skills"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate skill id", result.stderr.lower())
            self.assertFalse((out_dir / "skills.json").exists())

    def test_exports_items_and_skills_in_single_manifest(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "items.xlsx",
                ["id", "name"],
                [[1001, "Small Heal"]],
                "items",
            )
            self._write_workbook(
                source_dir / "skills.xlsx",
                ["id", "name", "required_class", "required_level", "skill_type", "target_type"],
                [[3, "Attack Training", "WARRIOR", 7, "PHYSICAL", "SINGLE_ENEMY"]],
                "skills",
            )

            result = self._run_export(source_dir, out_dir, ["items", "skills"])

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(
                [artifact["name"] for artifact in manifest["artifacts"]],
                ["items", "skills"],
            )


if __name__ == "__main__":
    unittest.main()
