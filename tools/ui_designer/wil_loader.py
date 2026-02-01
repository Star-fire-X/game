import os
import struct
from typing import List, Optional, Tuple

from PyQt5.QtGui import QImage, QPixmap


class WILLoader:
    """WIL/WIX frame loader (Legend2) returning QPixmap."""

    @staticmethod
    def load_wil_frame(wil_path: str, frame_index: int) -> Optional[QPixmap]:
        wil_path = os.path.abspath(wil_path)
        wix_path = WILLoader._find_wix_path(wil_path)
        offsets = WILLoader._read_wix_offsets(wix_path)
        if not offsets or frame_index < 0 or frame_index >= len(offsets):
            return None

        offset = offsets[frame_index]
        if offset <= 0:
            return None

        file_size = os.path.getsize(wil_path)
        header = WILLoader._read_wil_header(wil_path)
        if not header:
            return None

        bit_depth, palette, is_new_version, is_ilib, _, ver_flag = header
        next_offset = WILLoader._next_offset(offsets, frame_index, file_size)
        header_bytes = 8 if (is_ilib or is_new_version) else 12

        with open(wil_path, "rb") as fh:
            fh.seek(offset)
            if is_new_version or is_ilib:
                header_data = fh.read(8)
            else:
                header_data = fh.read(12)
                fh.read(4)  # skip bits pointer

            if len(header_data) < 8:
                print(f"读取失败: WIL 图像头不足 ({wil_path} offset={offset})")
                return None

            header_values = WILLoader._safe_unpack("<hhhh", header_data, f"WIL 图像头 {wil_path} offset={offset}")
            if not header_values:
                return None
            width, height, offset_x, offset_y = header_values

            payload_size = max(0, next_offset - offset - header_bytes)
            expected = WILLoader._calculate_data_size(width, height, bit_depth)
            expected_swapped = WILLoader._calculate_data_size(offset_x, height, bit_depth)
            if payload_size and expected_swapped == payload_size and expected != payload_size:
                width, offset_x = offset_x, width
                expected = expected_swapped

            data_size = expected if payload_size <= 0 else min(expected, payload_size)
            if width <= 0 or height <= 0 or data_size <= 0:
                return None

            raw_data = fh.read(data_size)
            if len(raw_data) < data_size:
                return None

        pixel_bytes = WILLoader._decode_pixels(raw_data, width, height, bit_depth, palette)
        if not pixel_bytes:
            return None

        image = QImage(pixel_bytes, width, height, QImage.Format_RGBA8888)
        return QPixmap.fromImage(image.copy())

    @staticmethod
    def _find_wix_path(wil_path: str) -> str:
        base, _ = os.path.splitext(wil_path)
        candidates = [f"{base}.WIX", f"{base}.wix", f"{base}.Wix"]
        for path in candidates:
            if os.path.exists(path):
                return path
        return candidates[0]

    @staticmethod
    def _read_wix_offsets(wix_path: str) -> List[int]:
        if not os.path.exists(wix_path):
            return []
        with open(wix_path, "rb") as fh:
            magic = fh.read(4)
            fh.seek(0)
            # INDX format
            if magic == b"#IND":
                title = fh.read(36)
                if not title.startswith(b"#INDX"):
                    return []
                _version = WILLoader._read_int32(fh, f"{wix_path} INDX version")
                _data_offset = WILLoader._read_int32(fh, f"{wix_path} INDX data_offset")
                index_count = WILLoader._read_int32(fh, f"{wix_path} INDX index_count")
                if _version is None or _data_offset is None or index_count is None:
                    return []
                if index_count <= 0 or index_count > 500_000:
                    return []
                expected_len = index_count * 4
                data = fh.read(expected_len)
                if len(data) < expected_len:
                    print(f"读取失败: WIX 索引数据不足 ({wix_path})")
                    return []
                return list(struct.unpack(f"<{index_count}i", data))
            # Legacy WIX
            title = fh.read(41)
            index_count = WILLoader._read_int32(fh, f"{wix_path} index_count")
            if index_count is None:
                return []
            ver_flag_bytes = fh.read(4)
            if len(ver_flag_bytes) < 4:
                print(f"读取失败: WIX ver_flag 不足 ({wix_path})")
            ver_flag = struct.unpack("<i", ver_flag_bytes)[0] if len(ver_flag_bytes) == 4 else 0
            is_new_version = ver_flag in (0, 65536)
            if is_new_version:
                fh.seek(41 + 4)  # title + index_count
            if index_count <= 0 or index_count > 500_000:
                file_size = os.path.getsize(wix_path)
                header_len = (41 + 4) if is_new_version else (41 + 8)
                if file_size > header_len:
                    index_count = (file_size - header_len) // 4
            expected_len = max(0, index_count) * 4
            data = fh.read(expected_len)
            if len(data) < expected_len:
                return []
            return list(struct.unpack(f"<{index_count}i", data))

    @staticmethod
    def _read_wil_header(wil_path: str) -> Optional[Tuple[int, List[int], bool, bool, int, int]]:
        if not os.path.exists(wil_path):
            return None
        with open(wil_path, "rb") as fh:
            magic = fh.read(4)
            fh.seek(0)
            is_ilib = magic == b"#ILI"
            if is_ilib:
                title = fh.read(36)
                if not title.startswith(b"#ILIB"):
                    return None
                header_data = fh.read(20)
                values = WILLoader._safe_unpack("<5i", header_data, f"WIL ILIB header {wil_path}")
                if not values:
                    return None
                _version, _data_offset, image_count, color_count, palette_size = values
                ver_flag = 0
            else:
                fh.read(41)  # title
                header_data = fh.read(16)
                values = WILLoader._safe_unpack("<4i", header_data, f"WIL header {wil_path}")
                if not values:
                    return None
                image_count, color_count, palette_size, ver_flag = values
            is_new_version = ver_flag == 0 or color_count == 65536
            bit_depth = {256: 8, 65536: 16, 16777216: 24}.get(color_count, 32)
            palette: List[int] = []
            if bit_depth == 8:
                palette_bytes = fh.read(256 * 4)
                if len(palette_bytes) < 256 * 4:
                    return None
                for i in range(256):
                    b = palette_bytes[i * 4 + 0]
                    g = palette_bytes[i * 4 + 1]
                    r = palette_bytes[i * 4 + 2]
                    a = 0 if (r == 0 and g == 0 and b == 0) else 255
                    palette.append((a << 24) | (b << 16) | (g << 8) | r)
            else:
                palette = [0xFF000000 | (i << 16) | (i << 8) | i for i in range(256)]
            return bit_depth, palette, is_new_version, is_ilib, image_count, ver_flag

    @staticmethod
    def _calculate_data_size(width: int, height: int, bit_depth: int) -> int:
        if width <= 0 or height <= 0:
            return 0
        if bit_depth == 8:
            row_bytes = ((width * 8 + 31) // 32) * 4
            return row_bytes * height
        return width * height * (bit_depth // 8)

    @staticmethod
    def _next_offset(offsets: List[int], index: int, file_size: int) -> int:
        for idx in range(index + 1, len(offsets)):
            if offsets[idx] > 0:
                return offsets[idx]
        return file_size

    @staticmethod
    def _decode_pixels(raw: bytes, width: int, height: int, bit_depth: int, palette: List[int]) -> Optional[bytes]:
        pixels = bytearray(width * height * 4)

        if bit_depth == 8:
            row_bytes = ((width * 8 + 31) // 32) * 4
            if len(raw) < row_bytes * height:
                return None
            for y in range(height):
                src_y = height - 1 - y
                base = src_y * row_bytes
                for x in range(width):
                    idx = raw[base + x]
                    color = palette[idx]
                    r = color & 0xFF
                    g = (color >> 8) & 0xFF
                    b = (color >> 16) & 0xFF
                    a = (color >> 24) & 0xFF
                    pos = (y * width + x) * 4
                    pixels[pos : pos + 4] = bytes((r, g, b, a))
        elif bit_depth == 16:
            row_bytes = width * 2
            if len(raw) < row_bytes * height:
                return None
            for y in range(height):
                src_y = height - 1 - y
                base = src_y * row_bytes
                for x in range(width):
                    lo = raw[base + x * 2]
                    hi = raw[base + x * 2 + 1]
                    pixel = lo | (hi << 8)
                    r = ((pixel >> 11) & 0x1F) << 3
                    g = ((pixel >> 5) & 0x3F) << 2
                    b = (pixel & 0x1F) << 3
                    a = 0 if pixel == 0 else 255
                    pos = (y * width + x) * 4
                    pixels[pos : pos + 4] = bytes((r, g, b, a))
        elif bit_depth == 24:
            row_bytes = width * 3
            if len(raw) < row_bytes * height:
                return None
            for y in range(height):
                src_y = height - 1 - y
                base = src_y * row_bytes
                for x in range(width):
                    b, g, r = raw[base + x * 3 : base + x * 3 + 3]
                    a = 0 if (r == 0 and g == 0 and b == 0) else 255
                    pos = (y * width + x) * 4
                    pixels[pos : pos + 4] = bytes((r, g, b, a))
        elif bit_depth == 32:
            row_bytes = width * 4
            if len(raw) < row_bytes * height:
                return None
            for y in range(height):
                src_y = height - 1 - y
                base = src_y * row_bytes
                for x in range(width):
                    b = raw[base + x * 4 + 0]
                    g = raw[base + x * 4 + 1]
                    r = raw[base + x * 4 + 2]
                    a = raw[base + x * 4 + 3]
                    if a == 0 and r == 0 and g == 0 and b == 0:
                        a = 0
                    elif a == 0:
                        a = 255
                    pos = (y * width + x) * 4
                    pixels[pos : pos + 4] = bytes((r, g, b, a))
        else:
            return None

        return bytes(pixels)

    @staticmethod
    def _safe_unpack(fmt: str, data: bytes, context: str) -> Optional[tuple]:
        expected = struct.calcsize(fmt)
        if len(data) < expected:
            print(f"读取失败: {context} (需要 {expected} 字节, 实际 {len(data)})")
            return None
        return struct.unpack(fmt, data[:expected])

    @staticmethod
    def _read_int32(fh, context: str) -> Optional[int]:
        data = fh.read(4)
        if len(data) < 4:
            print(f"读取失败: {context} (需要 4 字节, 实际 {len(data)})")
            return None
        return struct.unpack("<i", data)[0]
