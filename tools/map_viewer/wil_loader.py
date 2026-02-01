"""
WIL 资源加载器

用于加载 Legend2 的 WIL/WIX 图像档案，支持地图瓦片渲染。
复用 ui_designer 的 WIL 加载逻辑。
"""

import os
import struct
from typing import List, Optional, Tuple, Dict
from functools import lru_cache

from PyQt5.QtGui import QImage, QPixmap

def _read_env_int(name: str, default: int, min_value: int, max_value: int) -> int:
    try:
        value = int(os.environ.get(name, default))
    except (TypeError, ValueError):
        value = default
    return max(min_value, min(max_value, value))


# Increase frame cache to reduce thrashing when zoomed out on large maps.
# Override via MAP_VIEWER_FRAME_CACHE_SIZE for different machine profiles.
FRAME_CACHE_SIZE = _read_env_int("MAP_VIEWER_FRAME_CACHE_SIZE", 5000, 500, 20000)
MAX_SPRITE_WIDTH = _read_env_int("MAP_VIEWER_MAX_SPRITE_WIDTH", 2048, 128, 8192)
MAX_SPRITE_HEIGHT = _read_env_int("MAP_VIEWER_MAX_SPRITE_HEIGHT", 2048, 128, 8192)
MAX_SPRITE_PIXELS = _read_env_int("MAP_VIEWER_MAX_SPRITE_PIXELS", 2_000_000, 100_000, 20_000_000)


def _safe_unpack(fmt: str, data: bytes, context: str) -> Optional[tuple]:
    expected = struct.calcsize(fmt)
    if len(data) < expected:
        print(f"读取失败: {context} (需要 {expected} 字节, 实际 {len(data)})")
        return None
    return struct.unpack(fmt, data[:expected])


def _read_int32(fh, context: str) -> Optional[int]:
    data = fh.read(4)
    if len(data) < 4:
        print(f"读取失败: {context} (需要 4 字节, 实际 {len(data)})")
        return None
    return struct.unpack("<i", data)[0]


class WILArchive:
    """
    WIL 档案管理器
    
    负责加载和缓存 WIL 档案中的图像帧。
    """
    
    def __init__(self, wil_path: str):
        """
        初始化 WIL 档案
        
        Args:
            wil_path: WIL 文件路径
        """
        self.wil_path = os.path.abspath(wil_path)
        self.wix_path = self._find_wix_path(self.wil_path)
        self.archive_name = os.path.splitext(os.path.basename(wil_path))[0]
        
        # 读取索引
        self.offsets = self._read_wix_offsets(self.wix_path)
        self.frame_count = len(self.offsets)
        
        # 读取文件头
        self.header = self._read_wil_header(self.wil_path)
        if self.header:
            self.bit_depth, self.palette, self.is_new_version, self.is_ilib, self.image_count, self.ver_flag = self.header
        else:
            self.bit_depth = 8
            self.palette = []
            self.is_new_version = False
            self.is_ilib = False
            self.image_count = 0
            self.ver_flag = 0
        
        self.file_size = os.path.getsize(self.wil_path) if os.path.exists(self.wil_path) else 0
    
    def is_loaded(self) -> bool:
        """检查档案是否成功加载"""
        return len(self.offsets) > 0 and self.header is not None
    
    def get_frame_count(self) -> int:
        """获取档案中的图像帧数"""
        return self.frame_count

    @lru_cache(maxsize=FRAME_CACHE_SIZE)
    def _frame_header_info(
        self,
        frame_index: int,
    ) -> Optional[tuple[int, int, int, int, int, int]]:
        if not self.is_loaded() or frame_index < 0 or frame_index >= self.frame_count:
            return None

        offset = self.offsets[frame_index]
        if offset <= 0:
            return None

        next_offset = self._next_offset(frame_index)
        delta = max(0, next_offset - offset)

        try:
            with open(self.wil_path, "rb") as fh:
                fh.seek(offset)
                header_data = fh.read(8)

            if len(header_data) < 8:
                print(f"读取失败: WIL 图像头不足 ({self.wil_path} offset={offset})")
                return None

            width, height, offset_x, offset_y = struct.unpack("<hhhh", header_data)
        except Exception as e:
            print(f"读取 WIL 图像头失败 ({self.wil_path} offset={offset}): {e}")
            return None

        expected = self._calculate_data_size(width, height)
        expected_swapped = self._calculate_data_size(offset_x, height)

        def _payload_matches(payload: int, expected_size: int) -> bool:
            return expected_size > 0 and payload >= expected_size and (payload - expected_size) <= 16

        header_skip = 0
        payload_size = max(0, delta - 8) if delta > 0 else 0
        if delta > 0:
            payload_new = delta - 8
            payload_old = delta - 16
            new_matches = _payload_matches(payload_new, expected)
            new_swapped = _payload_matches(payload_new, expected_swapped)
            old_matches = _payload_matches(payload_old, expected)
            old_swapped = _payload_matches(payload_old, expected_swapped)

            if new_matches or new_swapped:
                header_skip = 0
                payload_size = payload_new
                if new_swapped and expected != expected_swapped:
                    width, offset_x = offset_x, width
            elif old_matches or old_swapped:
                header_skip = 8
                payload_size = payload_old
                if old_swapped and expected != expected_swapped:
                    width, offset_x = offset_x, width
            else:
                header_skip = 0 if (self.is_new_version or self.is_ilib) else 8
                payload_size = delta - (8 + header_skip)
                if payload_size < 0:
                    payload_size = 0
                if _payload_matches(payload_size, expected_swapped) and expected != expected_swapped:
                    width, offset_x = offset_x, width

        return (width, height, offset_x, offset_y, header_skip, payload_size)

    def get_frame_header(self, frame_index: int) -> Optional[tuple[int, int, int, int]]:
        """读取图像头部信息 (宽度/高度/偏移)"""
        info = self._frame_header_info(frame_index)
        if not info:
            return None
        width, height, offset_x, offset_y, _header_skip, _payload_size = info
        return width, height, offset_x, offset_y

    @lru_cache(maxsize=FRAME_CACHE_SIZE)
    def load_frame_with_offset(self, frame_index: int) -> Optional[tuple[QPixmap, int, int]]:
        """
        加载指定索引的图像帧 (包含偏移)
        
        Args:
            frame_index: 帧索引
            
        Returns:
            (QPixmap, offset_x, offset_y) 元组，失败返回 None
        """
        if not self.is_loaded() or frame_index < 0 or frame_index >= self.frame_count:
            return None
        
        offset = self.offsets[frame_index]
        if offset <= 0:
            return None

        header_info = self._frame_header_info(frame_index)
        if not header_info:
            return None
        width, height, offset_x, offset_y, header_skip, payload_size = header_info

        if width <= 0 or height <= 0:
            return None
        if width > MAX_SPRITE_WIDTH or height > MAX_SPRITE_HEIGHT:
            print(
                "警告: 精灵尺寸异常，跳过 "
                f"({width}x{height}) index={frame_index} {self.wil_path}"
            )
            return None
        if width * height > MAX_SPRITE_PIXELS:
            print(
                "警告: 精灵像素过大，跳过 "
                f"({width}x{height}) index={frame_index} {self.wil_path}"
            )
            return None

        expected = self._calculate_data_size(width, height)
        if expected <= 0:
            return None

        data_size = expected if payload_size <= 0 else min(expected, payload_size)
        if data_size <= 0:
            return None

        try:
            with open(self.wil_path, "rb") as fh:
                fh.seek(offset + 8 + header_skip)

                # 读取像素数据
                raw_data = fh.read(data_size)
                if len(raw_data) < data_size:
                    return None
            
            # 解码像素
            pixel_bytes = self._decode_pixels(raw_data, width, height)
            if not pixel_bytes:
                return None
            
            # 创建 QPixmap
            image = QImage(pixel_bytes, width, height, QImage.Format_RGBA8888)
            pixmap = QPixmap.fromImage(image.copy())
            return (pixmap, offset_x, offset_y)
            
        except Exception as e:
            print(f"加载图像帧 {frame_index} 失败: {e}")
            return None

    def load_frame(self, frame_index: int) -> Optional[QPixmap]:
        """
        加载指定索引的图像帧
        
        Args:
            frame_index: 帧索引
            
        Returns:
            QPixmap 对象，加载失败返回 None
        """
        result = self.load_frame_with_offset(frame_index)
        if result:
            return result[0]
        return None
    
    def _find_wix_path(self, wil_path: str) -> str:
        """查找对应的 WIX 文件"""
        base, _ = os.path.splitext(wil_path)
        candidates = [f"{base}.WIX", f"{base}.wix", f"{base}.Wix"]
        for path in candidates:
            if os.path.exists(path):
                return path
        return candidates[0]
    
    def _read_wix_offsets(self, wix_path: str) -> List[int]:
        """读取 WIX 索引文件中的偏移表"""
        if not os.path.exists(wix_path):
            return []
        
        try:
            with open(wix_path, "rb") as fh:
                magic = fh.read(4)
                fh.seek(0)
                
                # INDX 格式
                if magic == b"#IND":
                    title = fh.read(36)
                    if not title.startswith(b"#INDX"):
                        return []
                    _version = _read_int32(fh, f"{wix_path} INDX version")
                    _data_offset = _read_int32(fh, f"{wix_path} INDX data_offset")
                    index_count = _read_int32(fh, f"{wix_path} INDX index_count")
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

                file_size = os.path.getsize(wix_path)
                fh.seek(0)
                header_preview = fh.read(min(64, file_size))
                max_offset = min(len(header_preview) - 4, 64)
                for offset in range(32, max_offset + 1):
                    index_count = struct.unpack_from("<I", header_preview, offset)[0]
                    if index_count <= 0 or index_count > 500_000:
                        continue
                    header_len = offset + 4
                    if header_len + index_count * 4 != file_size:
                        continue
                    fh.seek(header_len)
                    expected_len = index_count * 4
                    data = fh.read(expected_len)
                    if len(data) < expected_len:
                        print(f"读取失败: WIX 索引数据不足 ({wix_path})")
                        return []
                    return list(struct.unpack(f"<{index_count}i", data))
                
                # 传统 WIX 格式
                fh.seek(0)
                title = fh.read(41)
                index_count = _read_int32(fh, f"{wix_path} index_count")
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
                
        except Exception as e:
            print(f"读取 WIX 文件失败: {e}")
            return []
    
    def _read_wil_header(self, wil_path: str) -> Optional[Tuple[int, List[int], bool, bool, int, int]]:
        """读取 WIL 文件头"""
        if not os.path.exists(wil_path):
            return None
        
        try:
            with open(wil_path, "rb") as fh:
                magic = fh.read(4)
                fh.seek(0)
                is_ilib = magic == b"#ILI"
                
                if is_ilib:
                    title = fh.read(36)
                    if not title.startswith(b"#ILIB"):
                        return None
                    header_data = fh.read(20)
                    values = _safe_unpack("<5i", header_data, f"WIL ILIB header {wil_path}")
                    if not values:
                        return None
                    _version, _data_offset, image_count, color_count, palette_size = values
                    ver_flag = 0
                else:
                    header_preview = fh.read(64)
                    values41 = None
                    values40 = None
                    if len(header_preview) >= 57:
                        values41 = struct.unpack_from("<4i", header_preview, 41)
                    if len(header_preview) >= 56:
                        values40 = struct.unpack_from("<4i", header_preview, 40)

                    def _valid_color(count: int, palette_size: int) -> bool:
                        return count in (256, 65536, 16777216) and palette_size in (0, 1024)

                    header_len = 41 + 16
                    image_count = 0
                    color_count = 0
                    palette_size = 0
                    ver_flag = 0

                    values41_ok = False
                    if values41:
                        image41, color41, pal41, flag41 = values41
                        values41_ok = _valid_color(color41, pal41)
                    values40_ok = False
                    if values40:
                        _version40, image40, color40, pal40 = values40
                        values40_ok = _valid_color(color40, pal40)

                    use40 = False
                    if values40_ok:
                        if self.frame_count and abs(image40 - self.frame_count) <= 1:
                            use40 = True
                        elif not values41_ok:
                            use40 = True

                    if use40 and values40:
                        _version40, image_count, color_count, palette_size = values40
                        ver_flag = 0
                        header_len = 40 + 16
                    elif values41 and values41_ok:
                        image_count, color_count, palette_size, ver_flag = values41
                        header_len = 41 + 16
                    elif values41:
                        image_count, color_count, palette_size, ver_flag = values41
                        header_len = 41 + 16
                    else:
                        return None

                    fh.seek(header_len)
                
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
                
        except Exception as e:
            print(f"读取 WIL 文件头失败: {e}")
            return None
    
    def _calculate_data_size(self, width: int, height: int) -> int:
        """计算图像数据大小"""
        if width <= 0 or height <= 0:
            return 0
        if self.bit_depth == 8:
            return width * height
        return width * height * (self.bit_depth // 8)
    
    def _next_offset(self, index: int) -> int:
        """获取下一个有效偏移"""
        for idx in range(index + 1, len(self.offsets)):
            if self.offsets[idx] > 0:
                return self.offsets[idx]
        return self.file_size
    
    def _decode_pixels(self, raw: bytes, width: int, height: int) -> Optional[bytes]:
        """解码像素数据为 RGBA 格式"""
        pixels = bytearray(width * height * 4)
        
        if self.bit_depth == 8:
            row_bytes = width
            if len(raw) < row_bytes * height:
                return None
            for y in range(height):
                src_y = height - 1 - y
                base = src_y * row_bytes
                for x in range(width):
                    idx = raw[base + x]
                    color = self.palette[idx]
                    r = color & 0xFF
                    g = (color >> 8) & 0xFF
                    b = (color >> 16) & 0xFF
                    a = (color >> 24) & 0xFF
                    pos = (y * width + x) * 4
                    pixels[pos : pos + 4] = bytes((r, g, b, a))
        elif self.bit_depth == 16:
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
        elif self.bit_depth == 24:
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
        elif self.bit_depth == 32:
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


class WILResourceManager:
    """
    WIL 资源管理器
    
    管理多个 WIL 档案，提供统一的访问接口。
    """
    
    def __init__(self):
        self.archives: Dict[str, WILArchive] = {}
        self.data_directory = ""
        self.missing_archives = set()
    
    def set_data_directory(self, directory: str):
        """设置 Data 目录路径"""
        self.data_directory = os.path.abspath(directory)
        self.missing_archives.clear()
    
    def load_archive(self, archive_name: str) -> bool:
        """
        加载 WIL 档案
        
        Args:
            archive_name: 档案名称（不含扩展名，如 "Tiles"）
            
        Returns:
            成功返回 True
        """
        if archive_name in self.missing_archives:
            return False
        if archive_name in self.archives:
            return True
        
        # 尝试多个可能的路径
        paths = []
        if self.data_directory:
            paths.append(os.path.join(self.data_directory, f"{archive_name}.wil"))
        
        # 相对路径
        paths.extend([
            f"Data/{archive_name}.wil",
            f"../Data/{archive_name}.wil",
            f"../../Data/{archive_name}.wil",
        ])
        
        for path in paths:
            if os.path.exists(path):
                try:
                    archive = WILArchive(path)
                    if archive.is_loaded():
                        self.archives[archive_name] = archive
                        self.missing_archives.discard(archive_name)
                        print(f"成功加载 WIL 档案: {archive_name} ({archive.get_frame_count()} 帧)")
                        return True
                except Exception as e:
                    print(f"加载 WIL 档案失败 {path}: {e}")
        
        print(f"未找到 WIL 档案: {archive_name}")
        self.missing_archives.add(archive_name)
        return False
    
    def get_frame(self, archive_name: str, frame_index: int) -> Optional[QPixmap]:
        """
        获取指定档案的图像帧
        
        Args:
            archive_name: 档案名称
            frame_index: 帧索引
            
        Returns:
            QPixmap 对象，失败返回 None
        """
        # 自动加载档案
        if archive_name not in self.archives:
            if not self.load_archive(archive_name):
                return None
        
        archive = self.archives.get(archive_name)
        if archive:
            return archive.load_frame(frame_index)
        return None

    def get_frame_header(
        self,
        archive_name: str,
        frame_index: int,
    ) -> Optional[tuple[int, int, int, int]]:
        """
        获取指定档案的图像头部信息 (宽度/高度/偏移)

        Args:
            archive_name: 档案名称
            frame_index: 帧索引

        Returns:
            (width, height, offset_x, offset_y) 元组，失败返回 None
        """
        if archive_name not in self.archives:
            if not self.load_archive(archive_name):
                return None

        archive = self.archives.get(archive_name)
        if archive:
            return archive.get_frame_header(frame_index)
        return None

    def get_frame_with_offset(self, archive_name: str, frame_index: int) -> Optional[tuple[QPixmap, int, int]]:
        """
        获取指定档案的图像帧与偏移
        
        Args:
            archive_name: 档案名称
            frame_index: 帧索引
            
        Returns:
            (QPixmap, offset_x, offset_y) 元组，失败返回 None
        """
        if archive_name not in self.archives:
            if not self.load_archive(archive_name):
                return None
        
        archive = self.archives.get(archive_name)
        if archive:
            return archive.load_frame_with_offset(frame_index)
        return None
    
    def is_archive_loaded(self, archive_name: str) -> bool:
        """检查档案是否已加载"""
        return archive_name in self.archives
    
    def get_frame_count(self, archive_name: str) -> int:
        """获取档案的帧数"""
        archive = self.archives.get(archive_name)
        return archive.get_frame_count() if archive else 0
    
    def clear_cache(self):
        """清除所有缓存"""
        for archive in self.archives.values():
            archive.load_frame.cache_clear()
