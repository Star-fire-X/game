import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from openpyxl import Workbook


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "export.py"


class ExportRuntimeTablesTests(unittest.TestCase):
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

    def _write_items_skills_workbooks(self, source_dir: Path, item_rows=None):
        self._write_workbook(
            source_dir / "items.xlsx",
            ["id", "name", "std_mode", "price", "stackable", "stack_limit"],
            item_rows or [[1001, "Small Heal", 0, 100, True, 20]],
            "items",
        )
        self._write_workbook(
            source_dir / "skills.xlsx",
            ["id", "name", "required_class", "required_level", "skill_type", "target_type"],
            [[3, "Attack Training", "WARRIOR", 7, "PHYSICAL", "SINGLE_ENEMY"]],
            "skills",
        )

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

    def test_exports_maps_gates_drops_json_and_manifest(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "items.xlsx",
                ["id", "name"],
                [[1001, "Small Heal"], [2001, "Silver Sword"], [2002, "Golden Sword"]],
                "items",
            )

            self._write_workbook(
                source_dir / "maps.xlsx",
                [
                    "id",
                    "is_safe_zone",
                    "min_level",
                    "home_map",
                    "fixes_json",
                    "safe_zones_json",
                    "quest_requirements_json",
                ],
                [
                    [
                        2,
                        True,
                        10,
                        "100",
                        '[{"x": 10, "y": 20}]',
                        '[{"x": 1, "y": 2, "radius": 3}]',
                        '[{"quest_id": 7, "quest_value": 11}]',
                    ],
                    [1, False, 1, "", "", "", ""],
                ],
                "maps",
            )
            self._write_workbook(
                source_dir / "gates.xlsx",
                [
                    "gate_id",
                    "source_map",
                    "source_x",
                    "source_y",
                    "target_map",
                    "target_x",
                    "target_y",
                    "require_item",
                    "required_item_id",
                ],
                [
                    [20, 2, 30, 40, 1, 50, 60, True, 1001],
                    [10, 1, 10, 20, 2, 30, 40, False, 0],
                ],
                "gates",
            )
            self._write_workbook(
                source_dir / "drops.xlsx",
                [
                    "monster_template_id",
                    "item_id",
                    "drop_rate",
                    "min_count",
                    "max_count",
                    "rarity",
                    "boss_bonus",
                ],
                [
                    [100, 2002, 0.25, 1, 2, 3, 0.5],
                    [100, 2001, 1.0, 1, 1, 1, 0.0],
                    [50, 1001, 0.1, 0, 1, 2, 0.0],
                ],
                "drops",
            )

            result = self._run_export(source_dir, out_dir, ["maps", "gates", "drops"])

            self.assertEqual(result.returncode, 0, msg=result.stderr)

            maps_text = (out_dir / "maps.json").read_text(encoding="utf-8")
            gates_text = (out_dir / "gates.json").read_text(encoding="utf-8")
            drops_text = (out_dir / "drops.json").read_text(encoding="utf-8")
            manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))

            maps_json = json.loads(maps_text)
            self.assertEqual([entry["map_id"] for entry in maps_json["maps"]], [1, 2])
            self.assertEqual(maps_json["maps"][0]["fixes"], [])
            self.assertEqual(maps_json["maps"][1]["safe_zones"][0]["radius"], 3)
            self.assertEqual(
                maps_json["maps"][1]["quest_requirements"][0]["quest_id"],
                7,
            )

            gates_json = json.loads(gates_text)
            self.assertEqual([entry["gate_id"] for entry in gates_json["gates"]], [10, 20])
            self.assertFalse(gates_json["gates"][0]["require_item"])
            self.assertEqual(gates_json["gates"][1]["required_item_id"], 1001)

            drops_json = json.loads(drops_text)
            self.assertEqual(
                [table["monster_template_id"] for table in drops_json["drop_tables"]],
                [50, 100],
            )
            self.assertEqual(
                [item["item_id"] for item in drops_json["drop_tables"][1]["items"]],
                [2001, 2002],
            )

            self.assertEqual(manifest["bundle_type"], "gameplay")
            self.assertEqual(manifest["generated_at"], "2026-03-07T00:00:00Z")
            self.assertEqual(self._artifact_by_name(manifest, "maps")["file"], "maps.json")
            self.assertEqual(self._artifact_by_name(manifest, "maps")["row_count"], 2)
            self.assertEqual(self._artifact_by_name(manifest, "gates")["file"], "gates.json")
            self.assertEqual(self._artifact_by_name(manifest, "gates")["row_count"], 2)
            self.assertEqual(self._artifact_by_name(manifest, "drops")["file"], "drops.json")
            self.assertEqual(self._artifact_by_name(manifest, "drops")["row_count"], 2)
            self.assertEqual(
                self._artifact_by_name(manifest, "maps")["hash"],
                hashlib.sha256(maps_text.encode("utf-8")).hexdigest(),
            )

    def test_rejects_missing_required_maps_headers(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "maps.xlsx",
                ["home_map", "fixes_json"],
                [["1", "[]"]],
                "maps",
            )

            result = self._run_export(source_dir, out_dir, ["maps"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing required columns", result.stderr.lower())
            self.assertFalse((out_dir / "maps.json").exists())

    def test_rejects_invalid_maps_json_cell(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "maps.xlsx",
                ["id", "fixes_json"],
                [[1, "{bad-json"]],
                "maps",
            )

            result = self._run_export(source_dir, out_dir, ["maps"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("fixes_json", result.stderr)

    def test_rejects_duplicate_gate_coordinates_and_invalid_require_item_combo(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "gates.xlsx",
                [
                    "gate_id",
                    "source_map",
                    "source_x",
                    "source_y",
                    "target_map",
                    "target_x",
                    "target_y",
                    "require_item",
                    "required_item_id",
                ],
                [
                    [10, 1, 10, 20, 2, 30, 40, False, 0],
                    [20, 1, 10, 20, 3, 50, 60, True, 0],
                ],
                "gates",
            )

            result = self._run_export(source_dir, out_dir, ["gates"])

            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(
                "duplicate gate source coordinate" in result.stderr.lower()
                or "required_item_id" in result.stderr.lower()
            )

    def test_rejects_duplicate_drop_entries_and_invalid_ranges(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "drops.xlsx",
                [
                    "monster_template_id",
                    "item_id",
                    "drop_rate",
                    "min_count",
                    "max_count",
                    "rarity",
                    "boss_bonus",
                ],
                [
                    [100, 2001, 1.2, 1, 2, 1, 0.0],
                    [100, 2001, 0.5, 3, 2, 1, 0.0],
                ],
                "drops",
            )

            result = self._run_export(source_dir, out_dir, ["drops"])

            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(
                "drop_rate" in result.stderr.lower()
                or "duplicate" in result.stderr.lower()
                or "min_count" in result.stderr.lower()
            )

    def test_exports_shops_json_and_manifest(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "items.xlsx",
                ["id", "name"],
                [[3001, "Potion"], [3002, "Sword"]],
                "items",
            )

            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [
                    [
                        200,
                        "premium",
                        1.5,
                        0.4,
                        '[{"item_id": 3002, "price": 50, "stock": 3}]',
                    ],
                    [
                        100,
                        "basic",
                        1.0,
                        0.5,
                        '[{"item_id": 3001, "price": 10, "stock": -1}]',
                    ],
                ],
                "shops",
            )

            result = self._run_export(source_dir, out_dir, ["shops"])

            self.assertEqual(result.returncode, 0, msg=result.stderr)

            shops_text = (out_dir / "shops.json").read_text(encoding="utf-8")
            manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
            shops_json = json.loads(shops_text)

            self.assertEqual([entry["store_id"] for entry in shops_json["shops"]], [100, 200])
            self.assertEqual(shops_json["shops"][0]["name"], "basic")
            self.assertEqual(shops_json["shops"][0]["items"][0]["stock"], -1)
            self.assertEqual(shops_json["shops"][1]["items"][0]["item_id"], 3002)
            self.assertEqual(self._artifact_by_name(manifest, "shops")["file"], "shops.json")
            self.assertEqual(self._artifact_by_name(manifest, "shops")["row_count"], 2)
            self.assertEqual(
                self._artifact_by_name(manifest, "shops")["hash"],
                hashlib.sha256(shops_text.encode("utf-8")).hexdigest(),
            )

    def test_rejects_missing_required_shops_headers(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name"],
                [[100, "basic"]],
                "shops",
            )

            result = self._run_export(source_dir, out_dir, ["shops"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing required columns", result.stderr.lower())
            self.assertFalse((out_dir / "shops.json").exists())

    def test_rejects_invalid_shops_items_json(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [[100, "basic", 1.0, 0.5, "{bad-json"]],
                "shops",
            )

            result = self._run_export(source_dir, out_dir, ["shops"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("items_json", result.stderr.lower())

    def test_rejects_duplicate_shop_ids(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)

            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [
                    [100, "basic", 1.0, 0.5, '[{"item_id": 3001, "price": 10, "stock": -1}]'],
                    [100, "duplicate", 1.2, 0.4, '[{"item_id": 3002, "price": 20, "stock": 5}]'],
                ],
                "shops",
            )

            result = self._run_export(source_dir, out_dir, ["shops"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate", result.stderr.lower())

    def test_rejects_duplicate_shop_item_ids_within_store(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)

            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [
                    [
                        100,
                        "basic",
                        1.0,
                        0.5,
                        '[{"item_id": 3001, "price": 10, "stock": -1}, '
                        '{"item_id": 3001, "price": 20, "stock": 5}]',
                    ],
                ],
                "shops",
            )

            result = self._run_export(source_dir, out_dir, ["shops"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate", result.stderr.lower())

    def test_rejects_invalid_shops_price(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)

            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [[100, "basic", 1.0, 0.5, '[{"item_id": 3001, "price": -1, "stock": 1}]']],
                "shops",
            )

            result = self._run_export(source_dir, out_dir, ["shops"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("price", result.stderr.lower())

    def test_rejects_invalid_shops_stock(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)

            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [[100, "basic", 1.0, 0.5, '[{"item_id": 3001, "price": 10, "stock": -2}]']],
                "shops",
            )

            result = self._run_export(source_dir, out_dir, ["shops"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("stock", result.stderr.lower())

    def test_rejects_shops_with_unknown_item_reference(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "items.xlsx",
                ["id", "name"],
                [[3002, "Sword"]],
                "items",
            )
            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [[100, "basic", 1.0, 0.5, '[{"item_id": 3001, "price": 10, "stock": -1}]']],
                "shops",
            )

            result = self._run_export(source_dir, out_dir, ["shops"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("shops", result.stderr.lower())
            self.assertIn("item_id", result.stderr.lower())

    def test_exports_monster_spawns_json_and_manifest(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "maps.xlsx",
                ["id"],
                [[99], [100]],
                "maps",
            )

            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                [
                    "spawn_id",
                    "map_id",
                    "center_x",
                    "center_y",
                    "spawn_radius",
                    "monster_template_id",
                    "patrol_radius",
                    "respawn_interval",
                    "max_count",
                    "aggro_range",
                    "attack_range",
                ],
                [
                    [2, 100, 20, 30, 4, 9002, 6, 15.0, 3, 12, 4],
                    [1, 99, 10, 15, 2, 9001, 5, 10.0, 2, 10, 3],
                ],
                "monster_spawns",
            )

            result = self._run_export(source_dir, out_dir, ["monster_spawns"])

            self.assertEqual(result.returncode, 0, msg=result.stderr)

            spawns_text = (out_dir / "monster_spawns.json").read_text(encoding="utf-8")
            manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
            spawns_json = json.loads(spawns_text)

            self.assertEqual([entry["spawn_id"] for entry in spawns_json["spawn_points"]], [1, 2])
            self.assertEqual(spawns_json["spawn_points"][0]["map_id"], 99)
            self.assertEqual(spawns_json["spawn_points"][1]["monster_template_id"], 9002)
            self.assertEqual(
                self._artifact_by_name(manifest, "monster_spawns")["file"],
                "monster_spawns.json",
            )
            self.assertEqual(self._artifact_by_name(manifest, "monster_spawns")["row_count"], 2)
            self.assertEqual(
                self._artifact_by_name(manifest, "monster_spawns")["hash"],
                hashlib.sha256(spawns_text.encode("utf-8")).hexdigest(),
            )

    def test_rejects_missing_required_monster_spawn_headers(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                ["spawn_id", "map_id"],
                [[1, 99]],
                "monster_spawns",
            )

            result = self._run_export(source_dir, out_dir, ["monster_spawns"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing required columns", result.stderr.lower())
            self.assertFalse((out_dir / "monster_spawns.json").exists())

    def test_rejects_duplicate_monster_spawn_ids(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                [
                    "spawn_id",
                    "map_id",
                    "center_x",
                    "center_y",
                    "spawn_radius",
                    "monster_template_id",
                    "patrol_radius",
                    "respawn_interval",
                    "max_count",
                    "aggro_range",
                    "attack_range",
                ],
                [
                    [1, 99, 10, 15, 2, 9001, 5, 10.0, 2, 10, 3],
                    [1, 100, 20, 30, 4, 9002, 6, 15.0, 3, 12, 4],
                ],
                "monster_spawns",
            )

            result = self._run_export(source_dir, out_dir, ["monster_spawns"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate", result.stderr.lower())

    def test_rejects_invalid_monster_spawn_values(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            headers = [
                "spawn_id",
                "map_id",
                "center_x",
                "center_y",
                "spawn_radius",
                "monster_template_id",
                "patrol_radius",
                "respawn_interval",
                "max_count",
                "aggro_range",
                "attack_range",
            ]

            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                headers,
                [[1, 0, 10, 15, 2, 9001, 5, 10.0, 2, 10, 3]],
                "monster_spawns",
            )
            result = self._run_export(source_dir, out_dir, ["monster_spawns"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("map_id", result.stderr.lower())

            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                headers,
                [[1, 99, 10, 15, 2, 0, 5, 10.0, 2, 10, 3]],
                "monster_spawns",
            )
            result = self._run_export(source_dir, out_dir, ["monster_spawns"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("monster_template_id", result.stderr.lower())

            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                headers,
                [[1, 99, 10, 15, -1, 9001, 5, 10.0, 2, 10, 3]],
                "monster_spawns",
            )
            result = self._run_export(source_dir, out_dir, ["monster_spawns"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("spawn_radius", result.stderr.lower())

            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                headers,
                [[1, 99, 10, 15, 2, 9001, -1, 10.0, 2, 10, 3]],
                "monster_spawns",
            )
            result = self._run_export(source_dir, out_dir, ["monster_spawns"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("patrol_radius", result.stderr.lower())

            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                headers,
                [[1, 99, 10, 15, 2, 9001, 5, -1.0, 2, 10, 3]],
                "monster_spawns",
            )
            result = self._run_export(source_dir, out_dir, ["monster_spawns"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("respawn_interval", result.stderr.lower())

            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                headers,
                [[1, 99, 10, 15, 2, 9001, 5, 10.0, 0, 10, 3]],
                "monster_spawns",
            )
            result = self._run_export(source_dir, out_dir, ["monster_spawns"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("max_count", result.stderr.lower())

    def test_rejects_monster_spawns_with_unknown_map_reference(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "maps.xlsx",
                ["id"],
                [[100]],
                "maps",
            )
            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                [
                    "spawn_id",
                    "map_id",
                    "center_x",
                    "center_y",
                    "spawn_radius",
                    "monster_template_id",
                    "patrol_radius",
                    "respawn_interval",
                    "max_count",
                    "aggro_range",
                    "attack_range",
                ],
                [[1, 99, 10, 15, 2, 9001, 5, 10.0, 2, 10, 3]],
                "monster_spawns",
            )

            result = self._run_export(source_dir, out_dir, ["monster_spawns"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("monster_spawns", result.stderr.lower())
            self.assertIn("map_id", result.stderr.lower())

    def test_exports_npcs_json_and_manifest(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "items.xlsx",
                ["id", "name"],
                [[3001, "Potion"], [3002, "Armor"]],
                "items",
            )
            self._write_workbook(
                source_dir / "maps.xlsx",
                ["id"],
                [[1]],
                "maps",
            )
            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [
                    [77, "potions", 1.0, 0.5, '[{"item_id": 3001, "price": 10, "stock": -1}]'],
                    [78, "armor", 1.2, 0.4, '[{"item_id": 3002, "price": 50, "stock": 3}]'],
                ],
                "shops",
            )
            self._write_workbook(
                source_dir / "npcs.xlsx",
                [
                    "npc_id",
                    "template_id",
                    "name",
                    "type",
                    "map_id",
                    "x",
                    "y",
                    "direction",
                    "enabled",
                    "store_id",
                ],
                [
                    [2, 2002, "Disabled Trader", "MERCHANT", 1, 20, 25, 3, False, 78],
                    [1, 2001, "Potion Trader", "MERCHANT", 1, 10, 15, 0, True, 77],
                ],
                "npcs",
            )

            result = self._run_export(source_dir, out_dir, ["npcs"])

            self.assertEqual(result.returncode, 0, msg=result.stderr)

            npcs_text = (out_dir / "npcs.json").read_text(encoding="utf-8")
            manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
            npcs_json = json.loads(npcs_text)

            self.assertEqual([entry["npc_id"] for entry in npcs_json["npcs"]], [1, 2])
            self.assertEqual(npcs_json["npcs"][0]["template_id"], 2001)
            self.assertTrue(npcs_json["npcs"][0]["enabled"])
            self.assertFalse(npcs_json["npcs"][1]["enabled"])
            self.assertEqual(npcs_json["npcs"][1]["store_id"], 78)
            self.assertEqual(self._artifact_by_name(manifest, "npcs")["file"], "npcs.json")
            self.assertEqual(self._artifact_by_name(manifest, "npcs")["row_count"], 2)
            self.assertEqual(
                self._artifact_by_name(manifest, "npcs")["hash"],
                hashlib.sha256(npcs_text.encode("utf-8")).hexdigest(),
            )

    def test_rejects_missing_required_npc_headers(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "npcs.xlsx",
                ["npc_id", "name"],
                [[1, "Potion Trader"]],
                "npcs",
            )

            result = self._run_export(source_dir, out_dir, ["npcs"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing required columns", result.stderr.lower())
            self.assertFalse((out_dir / "npcs.json").exists())

    def test_rejects_duplicate_npc_ids(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "npcs.xlsx",
                [
                    "npc_id",
                    "template_id",
                    "name",
                    "type",
                    "map_id",
                    "x",
                    "y",
                    "direction",
                    "enabled",
                    "store_id",
                ],
                [
                    [1, 2001, "Potion Trader", "MERCHANT", 1, 10, 15, 0, True, 77],
                    [1, 2002, "Armor Trader", "MERCHANT", 1, 12, 18, 1, True, 78],
                ],
                "npcs",
            )

            result = self._run_export(source_dir, out_dir, ["npcs"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate", result.stderr.lower())

    def test_rejects_invalid_npc_values(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            headers = [
                "npc_id",
                "template_id",
                "name",
                "type",
                "map_id",
                "x",
                "y",
                "direction",
                "enabled",
                "store_id",
            ]

            self._write_workbook(
                source_dir / "npcs.xlsx",
                headers,
                [[1, 0, "Potion Trader", "MERCHANT", 1, 10, 15, 0, True, 77]],
                "npcs",
            )
            result = self._run_export(source_dir, out_dir, ["npcs"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("template_id", result.stderr.lower())

            self._write_workbook(
                source_dir / "npcs.xlsx",
                headers,
                [[1, 2001, "Potion Trader", "QUEST", 1, 10, 15, 0, True, 77]],
                "npcs",
            )
            result = self._run_export(source_dir, out_dir, ["npcs"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("merchant", result.stderr.lower())

            self._write_workbook(
                source_dir / "npcs.xlsx",
                headers,
                [[1, 2001, "Potion Trader", "MERCHANT", 0, 10, 15, 0, True, 77]],
                "npcs",
            )
            result = self._run_export(source_dir, out_dir, ["npcs"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("map_id", result.stderr.lower())

            self._write_workbook(
                source_dir / "npcs.xlsx",
                headers,
                [[1, 2001, "Potion Trader", "MERCHANT", 1, -1, 15, 0, True, 77]],
                "npcs",
            )
            result = self._run_export(source_dir, out_dir, ["npcs"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("coordinates", result.stderr.lower())

            self._write_workbook(
                source_dir / "npcs.xlsx",
                headers,
                [[1, 2001, "Potion Trader", "MERCHANT", 1, 10, 15, 9, True, 77]],
                "npcs",
            )
            result = self._run_export(source_dir, out_dir, ["npcs"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("direction", result.stderr.lower())

            self._write_workbook(
                source_dir / "npcs.xlsx",
                headers,
                [[1, 2001, "Potion Trader", "MERCHANT", 1, 10, 15, 0, True, 0]],
                "npcs",
            )
            result = self._run_export(source_dir, out_dir, ["npcs"])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("store_id", result.stderr.lower())

    def test_rejects_npcs_with_unknown_shop_reference(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "items.xlsx",
                ["id", "name"],
                [[3001, "Potion"]],
                "items",
            )
            self._write_workbook(
                source_dir / "maps.xlsx",
                ["id"],
                [[1]],
                "maps",
            )
            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [[77, "potions", 1.0, 0.5, '[{"item_id": 3001, "price": 10, "stock": -1}]']],
                "shops",
            )
            self._write_workbook(
                source_dir / "npcs.xlsx",
                [
                    "npc_id",
                    "template_id",
                    "name",
                    "type",
                    "map_id",
                    "x",
                    "y",
                    "direction",
                    "enabled",
                    "store_id",
                ],
                [[1, 2001, "Potion Trader", "MERCHANT", 1, 10, 15, 0, True, 88]],
                "npcs",
            )

            result = self._run_export(source_dir, out_dir, ["npcs"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("npcs", result.stderr.lower())
            self.assertIn("store_id", result.stderr.lower())

    def test_rejects_gates_with_unknown_references(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_workbook(
                source_dir / "items.xlsx",
                ["id", "name"],
                [[1002, "Town Scroll"]],
                "items",
            )
            self._write_workbook(
                source_dir / "maps.xlsx",
                ["id"],
                [[1]],
                "maps",
            )
            self._write_workbook(
                source_dir / "gates.xlsx",
                [
                    "gate_id",
                    "source_map",
                    "source_x",
                    "source_y",
                    "target_map",
                    "target_x",
                    "target_y",
                    "require_item",
                    "required_item_id",
                ],
                [[10, 1, 10, 20, 2, 30, 40, True, 1001]],
                "gates",
            )

            result = self._run_export(source_dir, out_dir, ["gates"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("gates", result.stderr.lower())

    def test_rejects_drops_with_unknown_item_reference(self):
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
                source_dir / "drops.xlsx",
                [
                    "monster_template_id",
                    "item_id",
                    "drop_rate",
                    "min_count",
                    "max_count",
                    "rarity",
                    "boss_bonus",
                ],
                [[100, 2001, 0.5, 1, 2, 1, 0.0]],
                "drops",
            )

            result = self._run_export(source_dir, out_dir, ["drops"])

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("drops", result.stderr.lower())
            self.assertIn("item_id", result.stderr.lower())

    def test_exports_items_skills_maps_gates_drops_shops_monster_spawns_in_single_manifest(self):
        with tempfile.TemporaryDirectory() as source_tmp, tempfile.TemporaryDirectory() as out_tmp:
            source_dir = Path(source_tmp)
            out_dir = Path(out_tmp)
            self._write_items_skills_workbooks(
                source_dir,
                item_rows=[
                    [1001, "Small Heal", 0, 100, True, 20],
                    [2001, "Silver Sword", 1, 250, False, 1],
                ],
            )
            self._write_workbook(
                source_dir / "maps.xlsx",
                ["id"],
                [[1], [2]],
                "maps",
            )
            self._write_workbook(
                source_dir / "gates.xlsx",
                [
                    "gate_id",
                    "source_map",
                    "source_x",
                    "source_y",
                    "target_map",
                    "target_x",
                    "target_y",
                    "require_item",
                    "required_item_id",
                ],
                [[10, 1, 10, 20, 2, 30, 40, True, 1001]],
                "gates",
            )
            self._write_workbook(
                source_dir / "drops.xlsx",
                [
                    "monster_template_id",
                    "item_id",
                    "drop_rate",
                    "min_count",
                    "max_count",
                    "rarity",
                    "boss_bonus",
                ],
                [[100, 2001, 0.5, 1, 2, 1, 0.0]],
                "drops",
            )
            self._write_workbook(
                source_dir / "shops.xlsx",
                ["store_id", "name", "buy_rate", "sell_rate", "items_json"],
                [[100, "basic", 1.0, 0.5, '[{"item_id": 2001, "price": 10, "stock": -1}]']],
                "shops",
            )
            self._write_workbook(
                source_dir / "monster_spawns.xlsx",
                [
                    "spawn_id",
                    "map_id",
                    "center_x",
                    "center_y",
                    "spawn_radius",
                    "monster_template_id",
                    "patrol_radius",
                    "respawn_interval",
                    "max_count",
                    "aggro_range",
                    "attack_range",
                ],
                [[1, 1, 10, 20, 2, 9001, 5, 10.0, 2, 10, 3]],
                "monster_spawns",
            )
            self._write_workbook(
                source_dir / "npcs.xlsx",
                [
                    "npc_id",
                    "template_id",
                    "name",
                    "type",
                    "map_id",
                    "x",
                    "y",
                    "direction",
                    "enabled",
                    "store_id",
                ],
                [[1, 2001, "Potion Trader", "MERCHANT", 1, 10, 15, 0, True, 100]],
                "npcs",
            )

            result = self._run_export(
                source_dir,
                out_dir,
                ["items", "skills", "maps", "gates", "drops", "shops", "monster_spawns", "npcs"],
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["bundle_type"], "gameplay")
            self.assertEqual(
                [artifact["name"] for artifact in manifest["artifacts"]],
                ["drops", "gates", "items", "maps", "monster_spawns", "npcs", "shops", "skills"],
            )


if __name__ == "__main__":
    unittest.main()
