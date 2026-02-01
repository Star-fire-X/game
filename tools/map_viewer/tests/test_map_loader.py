import os
import os
import sys
import tempfile
import unittest
import struct

TEST_DIR = os.path.dirname(__file__)
sys.path.append(os.path.abspath(os.path.join(TEST_DIR, "..")))

from map_loader import MapLoader
from tile_data import MapTile


def write_map(path: str, width: int, height: int, header_size: int, tile_stride: int, first_tile: MapTile) -> None:
    with open(path, "wb") as f:
        f.write(struct.pack("<hh", width, height))
        if header_size > 4:
            f.write(b"\x00" * (header_size - 4))
        tile_count = width * height
        for i in range(tile_count):
            tile = first_tile if i == 0 else MapTile()
            f.write(tile.to_bytes())
            if tile_stride > MapTile.STRUCT_SIZE:
                f.write(b"\x00" * (tile_stride - MapTile.STRUCT_SIZE))


class MapLoaderTests(unittest.TestCase):
    def test_tile_from_bytes_short(self) -> None:
        with self.assertRaises(ValueError):
            MapTile.from_bytes(b"\x00")

    def test_load_standard_map(self) -> None:
        first = MapTile()
        first.background = 1
        first.middle = 2
        first.object = 3
        first.door_index = 4
        first.door_offset = 5
        first.anim_frame = 6
        first.anim_tick = 7
        first.area = 8
        first.light = 9

        with tempfile.NamedTemporaryFile(delete=False, suffix=".map") as tmp:
            path = tmp.name
        try:
            write_map(path, 2, 2, 52, 12, first)
            data = MapLoader.load(path)
            self.assertIsNotNone(data)
            assert data is not None
            self.assertEqual(data.width, 2)
            self.assertEqual(data.height, 2)
            tile = data.get_tile_by_index(0)
            self.assertIsNotNone(tile)
            assert tile is not None
            self.assertEqual(tile.background, 1)
            self.assertEqual(tile.middle, 2)
            self.assertEqual(tile.object, 3)
            self.assertEqual(tile.door_index, 4)
            self.assertEqual(tile.door_offset, 5)
            self.assertEqual(tile.anim_frame, 6)
            self.assertEqual(tile.anim_tick, 7)
            self.assertEqual(tile.area, 8)
            self.assertEqual(tile.light, 9)
        finally:
            os.unlink(path)

    def test_load_extended_header(self) -> None:
        first = MapTile()
        first.background = 10
        first.middle = 11
        first.object = 12

        with tempfile.NamedTemporaryFile(delete=False, suffix=".map") as tmp:
            path = tmp.name
        try:
            write_map(path, 3, 3, 64, 12, first)
            data = MapLoader.load(path)
            self.assertIsNotNone(data)
            assert data is not None
            tile = data.get_tile_by_index(0)
            self.assertIsNotNone(tile)
            assert tile is not None
            self.assertEqual(tile.background, 10)
            self.assertEqual(tile.middle, 11)
            self.assertEqual(tile.object, 12)
        finally:
            os.unlink(path)

    def test_load_event_header(self) -> None:
        first = MapTile()
        first.background = 21
        first.middle = 22
        first.object = 23

        with tempfile.NamedTemporaryFile(delete=False, suffix=".map") as tmp:
            path = tmp.name
        try:
            write_map(path, 4, 4, 8092, 12, first)
            data = MapLoader.load(path)
            self.assertIsNotNone(data)
            assert data is not None
            tile = data.get_tile_by_index(0)
            self.assertIsNotNone(tile)
            assert tile is not None
            self.assertEqual(tile.background, 21)
            self.assertEqual(tile.middle, 22)
            self.assertEqual(tile.object, 23)
        finally:
            os.unlink(path)

    def test_load_tile_stride_20(self) -> None:
        first = MapTile()
        first.background = 31
        first.middle = 32
        first.object = 33

        with tempfile.NamedTemporaryFile(delete=False, suffix=".map") as tmp:
            path = tmp.name
        try:
            write_map(path, 3, 2, 52, 20, first)
            data = MapLoader.load(path)
            self.assertIsNotNone(data)
            assert data is not None
            tile = data.get_tile_by_index(0)
            self.assertIsNotNone(tile)
            assert tile is not None
            self.assertEqual(tile.background, 31)
            self.assertEqual(tile.middle, 32)
            self.assertEqual(tile.object, 33)
        finally:
            os.unlink(path)

    def test_load_truncated_map(self) -> None:
        with tempfile.NamedTemporaryFile(delete=False, suffix=".map") as tmp:
            path = tmp.name
        try:
            with open(path, "wb") as f:
                f.write(struct.pack("<hh", 2, 2))
                f.write(b"\x00" * (52 - 4))
                f.write(b"\x00" * 6)
            data = MapLoader.load(path)
            self.assertIsNotNone(data)
            assert data is not None
            self.assertEqual(data.width, 2)
            self.assertEqual(data.height, 2)
        finally:
            os.unlink(path)

    def test_load_antihack_map(self) -> None:
        with tempfile.NamedTemporaryFile(delete=False, suffix=".map") as tmp:
            path = tmp.name
        try:
            with open(path, "wb") as f:
                width = 2
                height = 3
                key = 0xAA38
                enc_w = width ^ key
                enc_h = height ^ key
                header = bytearray(64)
                header[0:2] = struct.pack("<H", width)
                header[2:4] = struct.pack("<H", height)
                header[31:33] = struct.pack("<H", enc_w)
                header[33:35] = struct.pack("<H", key)
                header[35:37] = struct.pack("<H", enc_h)
                f.write(header[:52])
                f.write(b"\x00" * 12)
            data = MapLoader.load(path)
            self.assertIsNotNone(data)
            assert data is not None
            self.assertEqual(data.width, width)
            self.assertEqual(data.height, height)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
