"""
地图文件加载器

负责读取和保存 Legend2 的 .map 文件。
"""

import os
import struct
import shutil
import time
from typing import Optional
from pathlib import Path

from tile_data import MapTile, MapData, _TileArrays

BASE_HEADER_SIZE = 52  # 4 bytes (width/height) + 48 bytes metadata
MAP_FORMATS = [
    {"header_size": 52, "tile_size": 20, "name": "v2-20b-tiles"},
    {"header_size": 52, "tile_size": 12, "name": "v1-12b-tiles"},
    {"header_size": 64, "tile_size": 12, "name": "v1-12b-tiles+ext-header"},
    {"header_size": 8092, "tile_size": 12, "name": "v1-12b-tiles+event-header"},
    {"header_size": 9652, "tile_size": 12, "name": "v1-12b-tiles+extended-header"},
]
FALLBACK_TILE_SIZES = (12, 20, 16, 14)
MAX_TILE_COUNT = 5_000_000
DEFAULT_TILE_ORDER = os.environ.get("MAP_VIEWER_TILE_ORDER", "column")


def _detect_format(file_size: int, width: int, height: int) -> Optional[dict]:
    if width <= 0 or height <= 0:
        return None

    tile_count = width * height
    if tile_count <= 0:
        return None

    for fmt in MAP_FORMATS:
        expected = fmt["header_size"] + tile_count * fmt["tile_size"]
        if expected == file_size:
            return fmt

    for fmt in MAP_FORMATS:
        if file_size < fmt["header_size"]:
            continue
        available = file_size - fmt["header_size"]
        if available >= 0 and available % fmt["tile_size"] == 0:
            return fmt

    for tile_size in FALLBACK_TILE_SIZES:
        header_size = file_size - tile_count * tile_size
        if header_size >= BASE_HEADER_SIZE:
            return {"header_size": header_size, "tile_size": tile_size, "name": "unknown"}

    if file_size >= BASE_HEADER_SIZE:
        return {"header_size": BASE_HEADER_SIZE, "tile_size": 12, "name": "fallback"}

    return None


def _is_valid_dim(width: int, height: int) -> bool:
    return 0 < width <= 10000 and 0 < height <= 10000


def _read_dimensions(header: bytes) -> tuple[int, int, Optional[int]]:
    if len(header) < 4:
        return 0, 0, None

    def read_u16(offset: int) -> Optional[int]:
        if offset + 1 >= len(header):
            return None
        return header[offset] | (header[offset + 1] << 8)

    raw_w = read_u16(0) or 0
    raw_h = read_u16(2) or 0

    key = read_u16(33) if len(header) >= 35 else None
    if key == 0xAA38:
        enc_w = read_u16(31)
        enc_h = read_u16(35)
        if enc_w is not None and enc_h is not None:
            dec_w = enc_w ^ key
            dec_h = enc_h ^ key
            if _is_valid_dim(dec_w, dec_h):
                return dec_w, dec_h, key

    return raw_w, raw_h, None


def _normalize_tile_order(value: Optional[str]) -> str:
    if not value:
        return "auto"
    val = value.strip().lower()
    if val in ("row", "row-major", "rowmajor"):
        return "row"
    if val in ("column", "col", "column-major", "columnmajor"):
        return "column"
    if val in ("auto", "detect"):
        return "auto"
    return "auto"


def _tile_delta(file_tiles: _TileArrays, idx_a: int, idx_b: int) -> int:
    return (int(file_tiles.background[idx_a] != file_tiles.background[idx_b]) +
            int(file_tiles.middle[idx_a] != file_tiles.middle[idx_b]) +
            int(file_tiles.object[idx_a] != file_tiles.object[idx_b]))


def _tile_index_in_file(width: int, height: int, order: str, x: int, y: int) -> Optional[int]:
    if x < 0 or x >= width or y < 0 or y >= height:
        return None
    if order == "row":
        return y * width + x
    return x * height + y


def _score_tile_order(file_tiles: _TileArrays, width: int, height: int, order: str) -> float:
    if width <= 1 or height <= 1:
        return 0.0
    step_x = max(1, width // 64)
    step_y = max(1, height // 64)
    score = 0
    samples = 0
    for y in range(0, height - 1, step_y):
        for x in range(0, width - 1, step_x):
            idx = _tile_index_in_file(width, height, order, x, y)
            idx_right = _tile_index_in_file(width, height, order, x + 1, y)
            idx_down = _tile_index_in_file(width, height, order, x, y + 1)
            if idx is not None and idx_right is not None:
                score += _tile_delta(file_tiles, idx, idx_right)
                samples += 1
            if idx is not None and idx_down is not None:
                score += _tile_delta(file_tiles, idx, idx_down)
                samples += 1
    return score / samples if samples else 0.0

class MapLoader:
    """
    Legend2 地图文件加载器
    
    文件格式：
    - 文件头：2字节宽度 + 2字节高度 (uint16, little-endian)
    - 瓦片数据：每个瓦片 12 字节，按列优先顺序存储
    """
    
    @staticmethod
    def load(map_path: str) -> Optional[MapData]:
        """
        加载地图文件
        
        Args:
            map_path: 地图文件路径
            
        Returns:
            MapData 对象，如果加载失败返回 None
        """
        try:
            file_size = os.path.getsize(map_path)
            with open(map_path, 'rb') as f:
                # 读取文件头（最少 4 字节，尝试解析防破解头）
                header = f.read(64)
                if len(header) < 4:
                    print(f"错误: 文件头不完整: {map_path}")
                    return None

                width, height, anti_key = _read_dimensions(header)

                if width <= 0 or height <= 0:
                    print(f"错误: 无效的地图尺寸 {width}x{height}")
                    return None

                if width > 10000 or height > 10000:
                    print(f"警告: 地图尺寸异常大 {width}x{height}")

                format_info = _detect_format(file_size, width, height)
                if not format_info:
                    print(f"错误: 无法识别地图格式: {map_path} (size={file_size})")
                    return None

                header_size = int(format_info["header_size"])
                tile_stride = int(format_info["tile_size"])
                format_name = format_info.get("name", "unknown")

                if header_size < 4 or tile_stride < MapTile.STRUCT_SIZE:
                    print(f"错误: 地图格式异常 (header={header_size}, tile={tile_stride})")
                    return None

                tile_count = width * height
                if tile_count > MAX_TILE_COUNT:
                    print(
                        "错误: 地图瓦片数量过大 "
                        f"{tile_count} (> {MAX_TILE_COUNT}), 跳过: {map_path}"
                    )
                    return None

                expected_size = header_size + (tile_count * tile_stride)
                if file_size < expected_size:
                    print(f"警告: 瓦片数据不完整，预期 {expected_size} 字节，实际 {file_size} 字节")
                elif file_size > expected_size:
                    print(f"提示: 地图文件包含额外数据 {file_size - expected_size} 字节")

                # 跳过头部扩展区
                f.seek(header_size, os.SEEK_SET)

                # 创建地图数据对象
                try:
                    map_data = MapData(width, height)
                except MemoryError:
                    print(
                        "错误: 内存不足，无法分配瓦片数组: "
                        f"{map_path} ({width}x{height}, {tile_count} 瓦片)"
                    )
                    return None
                error_counts: dict[str, int] = {}
                error_samples: list[tuple[int, str]] = []

                def record_error(kind: str, tile_index: int, detail: str) -> None:
                    error_counts[kind] = error_counts.get(kind, 0) + 1
                    if len(error_samples) < 20:
                        error_samples.append((tile_index, detail))

                profile = os.environ.get("MAP_VIEWER_PROFILE", "0") == "1"
                parse_start = time.perf_counter() if profile else None
                tile_struct = struct.Struct(MapTile.STRUCT_FORMAT)
                raw_tiles = f.read(tile_count * tile_stride)
                raw_len = len(raw_tiles)
                if raw_len < tile_count * tile_stride:
                    record_error(
                        "short_read",
                        raw_len // tile_stride,
                        f"offset={header_size + raw_len} expected={tile_stride} actual={raw_len % tile_stride}"
                    )

                file_tiles = _TileArrays(tile_count)
                bg_count = 0
                mid_count = 0
                obj_count = 0
                walkable_count = 0
                has_animated = False

                mv = memoryview(raw_tiles)
                max_tiles = raw_len // tile_stride
                for i in range(min(tile_count, max_tiles)):
                    base = i * tile_stride
                    try:
                        (background, middle, object_index, door_index, door_offset,
                         anim_frame, anim_tick, area, light) = tile_struct.unpack_from(mv, base)
                    except Exception as e:
                        record_error(
                            type(e).__name__,
                            i,
                            f"offset={header_size + base} error={e}"
                        )
                        continue

                    file_tiles.background[i] = background
                    file_tiles.middle[i] = middle
                    file_tiles.object[i] = object_index
                    file_tiles.door_index[i] = door_index
                    file_tiles.door_offset[i] = door_offset
                    file_tiles.anim_frame[i] = anim_frame
                    file_tiles.anim_tick[i] = anim_tick
                    file_tiles.area[i] = area
                    file_tiles.light[i] = light

                    if background != 0:
                        bg_count += 1
                    if middle != 0:
                        mid_count += 1
                    if object_index != 0:
                        obj_count += 1
                    if (anim_frame & 0x7F) > 0:
                        has_animated = True
                    if (background & 0x8000) == 0 and (object_index & 0x8000) == 0:
                        has_door = (door_index & 0x80) != 0 and (door_index & 0x7F) != 0
                        if not has_door or (door_offset & 0x80) != 0:
                            walkable_count += 1

                tile_order = _normalize_tile_order(DEFAULT_TILE_ORDER)
                if tile_order == "auto":
                    row_score = _score_tile_order(file_tiles, width, height, "row")
                    col_score = _score_tile_order(file_tiles, width, height, "column")
                    tile_order = "row" if row_score <= col_score else "column"
                    print(f"瓦片顺序自动检测: row={row_score:.3f} col={col_score:.3f} -> {tile_order}")

                if tile_order == "row":
                    map_data._set_arrays(file_tiles)
                else:
                    out_bg = map_data._background
                    out_mid = map_data._middle
                    out_obj = map_data._object
                    out_door_index = map_data._door_index
                    out_door_offset = map_data._door_offset
                    out_anim_frame = map_data._anim_frame
                    out_anim_tick = map_data._anim_tick
                    out_area = map_data._area
                    out_light = map_data._light
                    for x in range(width):
                        base = x * height
                        for y in range(height):
                            in_index = base + y
                            out_index = y * width + x
                            out_bg[out_index] = file_tiles.background[in_index]
                            out_mid[out_index] = file_tiles.middle[in_index]
                            out_obj[out_index] = file_tiles.object[in_index]
                            out_door_index[out_index] = file_tiles.door_index[in_index]
                            out_door_offset[out_index] = file_tiles.door_offset[in_index]
                            out_anim_frame[out_index] = file_tiles.anim_frame[in_index]
                            out_anim_tick[out_index] = file_tiles.anim_tick[in_index]
                            out_area[out_index] = file_tiles.area[in_index]
                            out_light[out_index] = file_tiles.light[in_index]
                map_data.tile_order = tile_order
                map_data.layer_counts = {
                    "background": bg_count,
                    "middle": mid_count,
                    "object": obj_count,
                }
                map_data.walkable_count = walkable_count
                map_data.has_animated_tiles = has_animated

                if profile and parse_start is not None:
                    parse_time = time.perf_counter() - parse_start
                    print(f"MapLoader: parsed {tile_count} tiles in {parse_time:.3f}s")

                map_data.parse_errors = error_counts
                map_data.error_samples = error_samples

                if error_counts:
                    total_errors = sum(error_counts.values())
                    print(f"警告: 地图解析出现 {total_errors} 个瓦片错误 ({map_path})")
                    for kind, count in sorted(error_counts.items()):
                        print(f"  - {kind}: {count}")
                    for tile_index, detail in error_samples:
                        if tile_order == "row":
                            x = tile_index % width
                            y = tile_index // width
                        else:
                            x = tile_index // height
                            y = tile_index % height
                        print(f"  * tile {tile_index} ({x},{y}) {detail}")

                print(f"成功加载地图: {map_path} ({width}x{height}, {tile_count} 瓦片, {format_name})")
                return map_data

        except FileNotFoundError:
            print(f"错误: 文件不存在: {map_path}")
            return None
        except Exception as e:
            print(f"错误: 加载地图文件失败: {e}")
            import traceback
            traceback.print_exc()
            return None
    
    @staticmethod
    def save(map_data: MapData, map_path: str, backup: bool = True) -> bool:
        """
        保存地图文件
        
        Args:
            map_data: 地图数据对象
            map_path: 保存路径
            backup: 是否在保存前备份原文件
            
        Returns:
            成功返回 True，失败返回 False
        """
        try:
            # 验证地图数据
            if not map_data.is_valid():
                print(f"错误: 无效的地图数据")
                return False
            
            # 备份原文件
            if backup and os.path.exists(map_path):
                backup_path = map_path + '.backup'
                try:
                    shutil.copy2(map_path, backup_path)
                    print(f"备份原文件: {backup_path}")
                except Exception as e:
                    print(f"警告: 备份文件失败: {e}")
            
            # 确保目标目录存在
            Path(map_path).parent.mkdir(parents=True, exist_ok=True)
            
            # 写入文件
            with open(map_path, 'wb') as f:
                # 写入文件头（宽度和高度）
                header = struct.pack('<HH', map_data.width, map_data.height)
                f.write(header)
                if BASE_HEADER_SIZE > 4:
                    f.write(b"\x00" * (BASE_HEADER_SIZE - 4))

            # 写入瓦片数据（默认列优先，可通过 map_data.tile_order 控制）
            tile_order = getattr(map_data, "tile_order", "column")
            tile_struct = struct.Struct(MapTile.STRUCT_FORMAT)
            bg = map_data._background
            mid = map_data._middle
            obj = map_data._object
            door_index = map_data._door_index
            door_offset = map_data._door_offset
            anim_frame = map_data._anim_frame
            anim_tick = map_data._anim_tick
            area = map_data._area
            light = map_data._light
            if tile_order == "row":
                for i in range(map_data.get_tile_count()):
                    f.write(tile_struct.pack(
                        bg[i],
                        mid[i],
                        obj[i],
                        door_index[i],
                        door_offset[i],
                        anim_frame[i],
                        anim_tick[i],
                        area[i],
                        light[i],
                    ))
            else:
                for x in range(map_data.width):
                    for y in range(map_data.height):
                        idx = y * map_data.width + x
                        f.write(tile_struct.pack(
                            bg[idx],
                            mid[idx],
                            obj[idx],
                            door_index[idx],
                            door_offset[idx],
                            anim_frame[idx],
                            anim_tick[idx],
                            area[idx],
                            light[idx],
                        ))
            
            print(f"成功保存地图: {map_path} ({map_data.width}x{map_data.height})")
            return True
            
        except Exception as e:
            print(f"错误: 保存地图文件失败: {e}")
            import traceback
            traceback.print_exc()
            return False
    
    @staticmethod
    def get_map_info(map_path: str) -> Optional[dict]:
        """
        快速读取地图基本信息（只读取文件头）
        
        Args:
            map_path: 地图文件路径
            
        Returns:
            包含地图信息的字典，失败返回 None
        """
        try:
            with open(map_path, 'rb') as f:
                header = f.read(64)
                if len(header) < 4:
                    return None

                width, height = _read_dimensions(header)

                # 获取文件大小
                f.seek(0, 2)  # 移到文件末尾
                file_size = f.tell()

                format_info = _detect_format(file_size, width, height)
                if not format_info:
                    return {
                        'path': map_path,
                        'filename': os.path.basename(map_path),
                        'width': width,
                        'height': height,
                        'tile_count': width * height,
                        'file_size': file_size,
                        'expected_size': None,
                        'header_size': None,
                        'tile_size': None,
                        'format': 'unknown',
                        'is_complete': False
                    }

                header_size = int(format_info["header_size"])
                tile_size = int(format_info["tile_size"])
                expected_size = header_size + width * height * tile_size
                
                return {
                    'path': map_path,
                    'filename': os.path.basename(map_path),
                    'width': width,
                    'height': height,
                    'tile_count': width * height,
                    'file_size': file_size,
                    'expected_size': expected_size,
                    'header_size': header_size,
                    'tile_size': tile_size,
                    'format': format_info.get("name", "unknown"),
                    'is_complete': file_size >= expected_size
                }
                
        except Exception as e:
            print(f"错误: 读取地图信息失败: {e}")
            return None
    
    @staticmethod
    def validate_map_file(map_path: str) -> tuple[bool, str]:
        """
        验证地图文件的完整性
        
        Args:
            map_path: 地图文件路径
            
        Returns:
            (是否有效, 错误信息) 的元组
        """
        try:
            info = MapLoader.get_map_info(map_path)
            if info is None:
                return False, "无法读取地图文件"
            
            if info['width'] <= 0 or info['height'] <= 0:
                return False, f"无效的地图尺寸: {info['width']}x{info['height']}"

            if info.get('expected_size') is None:
                return False, "无法识别地图格式"
            
            if not info['is_complete']:
                return False, (f"文件大小不匹配: 预期 {info['expected_size']} 字节，"
                              f"实际 {info['file_size']} 字节")
            
            return True, "地图文件有效"
            
        except Exception as e:
            return False, f"验证失败: {str(e)}"


def list_map_files(directory: str) -> list[str]:
    """
    列出目录下所有的 .map 文件
    
    Args:
        directory: 目录路径
        
    Returns:
        .map 文件路径列表
    """
    map_files = []
    try:
        for root, dirs, files in os.walk(directory):
            for filename in files:
                if filename.lower().endswith('.map'):
                    map_files.append(os.path.join(root, filename))
        map_files.sort()
    except Exception as e:
        print(f"错误: 列出地图文件失败: {e}")
    
    return map_files


if __name__ == '__main__':
    # 测试代码
    import sys
    
    if len(sys.argv) < 2:
        print("用法: python map_loader.py <map_file_or_directory>")
        sys.exit(1)
    
    map_path = sys.argv[1]

    if os.path.isdir(map_path):
        files = list_map_files(map_path)
        failed = 0
        failed_paths: list[str] = []
        error_files = 0
        total_errors: dict[str, int] = {}
        print(f"开始扫描目录: {map_path} (共 {len(files)} 个文件)")
        for path in files:
            data = MapLoader.load(path)
            if not data:
                failed += 1
                if len(failed_paths) < 20:
                    failed_paths.append(path)
                continue
            errors = getattr(data, "parse_errors", {})
            if errors:
                error_files += 1
                for kind, count in errors.items():
                    total_errors[kind] = total_errors.get(kind, 0) + count
        print(f"扫描完成: 成功 {len(files) - failed} / 失败 {failed}")
        if failed_paths:
            print("失败文件示例 (前 20 个):")
            for path in failed_paths:
                print(f"  - {path}")
        if total_errors:
            print(f"解析异常文件数: {error_files}")
            for kind, count in sorted(total_errors.items()):
                print(f"  - {kind}: {count}")
        sys.exit(0 if failed == 0 else 1)
    
    # 加载地图
    map_data = MapLoader.load(map_path)
    if map_data:
        print(f"\n地图信息:")
        print(f"  尺寸: {map_data.width}x{map_data.height}")
        print(f"  瓦片总数: {map_data.get_tile_count()}")
        
        # 统计可行走瓦片
        walkable_count = map_data.get_walkable_count()
        print(f"  可行走瓦片: {walkable_count} ({walkable_count*100.0/map_data.get_tile_count():.1f}%)")
        
        # 统计各层非空瓦片
        bg_count = map_data.get_layer_count("background")
        mid_count = map_data.get_layer_count("middle")
        obj_count = map_data.get_layer_count("object")
        
        print(f"  背景层: {bg_count} 瓦片")
        print(f"  中间层: {mid_count} 瓦片")
        print(f"  物件层: {obj_count} 瓦片")
        
        # 显示前几个瓦片
        print(f"\n前 5 个瓦片:")
        for i in range(min(5, map_data.get_tile_count())):
            tile = map_data.get_tile_by_index(i)
            x = i % map_data.width
            y = i // map_data.width
            print(f"  [{x},{y}] {tile}")
