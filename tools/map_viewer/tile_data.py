"""
瓦片数据结构

定义 Legend2 地图的瓦片和地图数据结构，
与 C++ 代码中的 MapTile 和 MapData 对应。
"""

import struct
from array import array
from typing import Iterable, List, Optional, Tuple


class MapTile:
    """
    地图瓦片数据
    
    每个瓦片 12 字节，包含三层图像索引和各种属性。
    """
    
    # 结构体格式: 3个uint16 + 6个uint8 (共 12 字节)
    STRUCT_FORMAT = '<HHHBBBBBB'
    STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)
    
    __slots__ = (
        "background",
        "middle",
        "object",
        "door_index",
        "door_offset",
        "anim_frame",
        "anim_tick",
        "area",
        "light",
    )

    def __init__(self):
        self.background: int = 0      # 背景层瓦片索引 (Tiles.wil)
        self.middle: int = 0          # 中间层瓦片索引 (SmTiles.wil)
        self.object: int = 0          # 物体层瓦片索引 (Objects.wil)
        self.door_index: int = 0      # 门索引（用于传送门/门）
        self.door_offset: int = 0     # 门偏移
        self.anim_frame: int = 0      # 动画帧数
        self.anim_tick: int = 0       # 动画速度
        self.area: int = 0            # 区域标识符
        self.light: int = 0           # 光照强度（0..4）
    
    def is_walkable(self) -> bool:
        """
        检查瓦片是否可行走
        
        在传奇2地图中：
        - 背景/物体层高位(0x8000)表示阻挡
        - 门存在且未打开时不可通行
        """
        if (self.background & 0x8000) != 0:
            return False
        if (self.object & 0x8000) != 0:
            return False
        has_door = (self.door_index & 0x80) != 0 and (self.door_index & 0x7F) != 0
        if has_door and (self.door_offset & 0x80) == 0:
            return False
        return True
    
    def has_portal(self) -> bool:
        """检查瓦片是否有传送门/门"""
        return (self.door_index & 0x80) != 0 and (self.door_index & 0x7F) != 0
    
    def get_object_index(self) -> int:
        """获取物件层的实际索引（屏蔽高位阻挡标志）"""
        return self.object & 0x7FFF
    
    def set_object_index(self, index: int, blocked: bool = False):
        """
        设置物件层索引
        
        Args:
            index: 物件索引
            blocked: 是否设置阻挡标志
        """
        if blocked:
            self.object = index | 0x8000
        else:
            self.object = index & 0x7FFF
    
    def to_bytes(self) -> bytes:
        """将瓦片数据序列化为字节"""
        return struct.pack(
            self.STRUCT_FORMAT,
            self.background,
            self.middle,
            self.object,
            self.door_index,
            self.door_offset,
            self.anim_frame,
            self.anim_tick,
            self.area,
            self.light
        )
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'MapTile':
        """从字节反序列化瓦片数据"""
        if len(data) < cls.STRUCT_SIZE:
            raise ValueError(f"buffer too small: expected {cls.STRUCT_SIZE}, got {len(data)}")
        tile = cls()
        values = struct.unpack(cls.STRUCT_FORMAT, data[:cls.STRUCT_SIZE])
        tile.background = values[0]
        tile.middle = values[1]
        tile.object = values[2]
        tile.door_index = values[3]
        tile.door_offset = values[4]
        tile.anim_frame = values[5]
        tile.anim_tick = values[6]
        tile.area = values[7]
        tile.light = values[8]
        return tile

    @classmethod
    def from_values(
        cls,
        background: int,
        middle: int,
        object_index: int,
        door_index: int,
        door_offset: int,
        anim_frame: int,
        anim_tick: int,
        area: int,
        light: int,
    ) -> 'MapTile':
        """从字段值构造瓦片对象"""
        tile = cls()
        tile.background = background
        tile.middle = middle
        tile.object = object_index
        tile.door_index = door_index
        tile.door_offset = door_offset
        tile.anim_frame = anim_frame
        tile.anim_tick = anim_tick
        tile.area = area
        tile.light = light
        return tile
    
    def copy(self) -> 'MapTile':
        """创建瓦片的深拷贝"""
        tile = MapTile()
        tile.background = self.background
        tile.middle = self.middle
        tile.object = self.object
        tile.door_index = self.door_index
        tile.door_offset = self.door_offset
        tile.anim_frame = self.anim_frame
        tile.anim_tick = self.anim_tick
        tile.area = self.area
        tile.light = self.light
        return tile
    
    def __repr__(self) -> str:
        return (f"MapTile(bg={self.background}, mid={self.middle}, "
                f"obj={self.object}, light={self.light})")


class MapData:
    """
    地图数据
    
    包含地图尺寸和所有瓦片数据。
    """
    
    def __init__(self, width: int = 0, height: int = 0):
        self.width: int = width
        self.height: int = height
        self.tile_order: str = "column"
        self._tile_count: int = 0
        self._background = array('H')
        self._middle = array('H')
        self._object = array('H')
        self._door_index = array('B')
        self._door_offset = array('B')
        self._anim_frame = array('B')
        self._anim_tick = array('B')
        self._area = array('B')
        self._light = array('B')
        self.tiles = _MapTileSequence(self)
        self.layer_counts: Optional[dict[str, int]] = None
        self.walkable_count: Optional[int] = None
        self.has_animated_tiles: Optional[bool] = None

        if width > 0 and height > 0:
            self._allocate_arrays(width * height)

    def _allocate_arrays(self, tile_count: int) -> None:
        self._tile_count = tile_count
        self._background = array('H', [0]) * tile_count
        self._middle = array('H', [0]) * tile_count
        self._object = array('H', [0]) * tile_count
        self._door_index = array('B', [0]) * tile_count
        self._door_offset = array('B', [0]) * tile_count
        self._anim_frame = array('B', [0]) * tile_count
        self._anim_tick = array('B', [0]) * tile_count
        self._area = array('B', [0]) * tile_count
        self._light = array('B', [0]) * tile_count

    def _set_arrays(self, arrays: "_TileArrays") -> None:
        self._background = arrays.background
        self._middle = arrays.middle
        self._object = arrays.object
        self._door_index = arrays.door_index
        self._door_offset = arrays.door_offset
        self._anim_frame = arrays.anim_frame
        self._anim_tick = arrays.anim_tick
        self._area = arrays.area
        self._light = arrays.light
        self._tile_count = len(self._background)

    def _index(self, x: int, y: int) -> Optional[int]:
        if x < 0 or x >= self.width or y < 0 or y >= self.height:
            return None
        return y * self.width + x

    def get_tile_fields(self, x: int, y: int) -> Optional[Tuple[int, int, int, int, int, int, int, int, int]]:
        index = self._index(x, y)
        if index is None:
            return None
        return (
            self._background[index],
            self._middle[index],
            self._object[index],
            self._door_index[index],
            self._door_offset[index],
            self._anim_frame[index],
            self._anim_tick[index],
            self._area[index],
            self._light[index],
        )

    def get_tile_fields_by_index(self, index: int) -> Optional[Tuple[int, int, int, int, int, int, int, int, int]]:
        if index < 0 or index >= self._tile_count:
            return None
        return (
            self._background[index],
            self._middle[index],
            self._object[index],
            self._door_index[index],
            self._door_offset[index],
            self._anim_frame[index],
            self._anim_tick[index],
            self._area[index],
            self._light[index],
        )

    def get_object_fields_by_index(self, index: int) -> Optional[Tuple[int, int, int, int, int, int]]:
        if index < 0 or index >= self._tile_count:
            return None
        return (
            self._object[index],
            self._door_index[index],
            self._door_offset[index],
            self._anim_frame[index],
            self._anim_tick[index],
            self._area[index],
        )

    def set_tile_fields_by_index(
        self,
        index: int,
        background: int,
        middle: int,
        object_index: int,
        door_index: int,
        door_offset: int,
        anim_frame: int,
        anim_tick: int,
        area: int,
        light: int,
    ) -> bool:
        if index < 0 or index >= self._tile_count:
            return False
        self._background[index] = background
        self._middle[index] = middle
        self._object[index] = object_index
        self._door_index[index] = door_index
        self._door_offset[index] = door_offset
        self._anim_frame[index] = anim_frame
        self._anim_tick[index] = anim_tick
        self._area[index] = area
        self._light[index] = light
        return True
    
    def get_tile(self, x: int, y: int) -> Optional[MapTile]:
        """
        获取指定位置的瓦片
        
        Args:
            x: X 坐标
            y: Y 坐标
            
        Returns:
            瓦片对象，如果坐标无效则返回 None
        """
        index = self._index(x, y)
        if index is None:
            return None
        return MapTile.from_values(
            self._background[index],
            self._middle[index],
            self._object[index],
            self._door_index[index],
            self._door_offset[index],
            self._anim_frame[index],
            self._anim_tick[index],
            self._area[index],
            self._light[index],
        )

    def get_tile_by_index(self, index: int) -> Optional[MapTile]:
        if index < 0 or index >= self._tile_count:
            return None
        return MapTile.from_values(
            self._background[index],
            self._middle[index],
            self._object[index],
            self._door_index[index],
            self._door_offset[index],
            self._anim_frame[index],
            self._anim_tick[index],
            self._area[index],
            self._light[index],
        )
    
    def set_tile(self, x: int, y: int, tile: MapTile) -> bool:
        """
        设置指定位置的瓦片
        
        Args:
            x: X 坐标
            y: Y 坐标
            tile: 瓦片对象
            
        Returns:
            成功返回 True，坐标无效返回 False
        """
        index = self._index(x, y)
        if index is None:
            return False
        return self.set_tile_fields_by_index(
            index,
            tile.background,
            tile.middle,
            tile.object,
            tile.door_index,
            tile.door_offset,
            tile.anim_frame,
            tile.anim_tick,
            tile.area,
            tile.light,
        )
    
    def is_valid_position(self, x: int, y: int) -> bool:
        """检查位置是否在地图范围内"""
        return 0 <= x < self.width and 0 <= y < self.height
    
    def is_walkable(self, x: int, y: int) -> bool:
        """检查位置是否可行走"""
        tile = self.get_tile(x, y)
        return tile is not None and tile.is_walkable()
    
    def get_tile_count(self) -> int:
        """获取瓦片总数"""
        return self._tile_count
    
    def is_valid(self) -> bool:
        """检查地图数据是否有效"""
        return (self.width > 0 and self.height > 0 and
                self._tile_count == self.width * self.height and
                len(self._background) == self._tile_count)

    def get_layer_count(self, layer: str) -> int:
        if self.layer_counts and layer in self.layer_counts:
            return self.layer_counts[layer]
        if layer == "background":
            return sum(1 for value in self._background if value != 0)
        if layer == "middle":
            return sum(1 for value in self._middle if value != 0)
        if layer == "object":
            return sum(1 for value in self._object if value != 0)
        return 0

    def get_walkable_count(self) -> int:
        if self.walkable_count is not None:
            return self.walkable_count
        count = 0
        for index in range(self._tile_count):
            background = self._background[index]
            object_index = self._object[index]
            door_index = self._door_index[index]
            door_offset = self._door_offset[index]
            if (background & 0x8000) != 0:
                continue
            if (object_index & 0x8000) != 0:
                continue
            has_door = (door_index & 0x80) != 0 and (door_index & 0x7F) != 0
            if has_door and (door_offset & 0x80) == 0:
                continue
            count += 1
        self.walkable_count = count
        return count

    def get_has_animated_tiles(self) -> bool:
        if self.has_animated_tiles is not None:
            return self.has_animated_tiles
        for value in self._anim_frame:
            if (value & 0x7F) > 0:
                self.has_animated_tiles = True
                return True
        self.has_animated_tiles = False
        return False
    
    def build_walkability_matrix(self) -> List[List[bool]]:
        """构建可行走性矩阵"""
        matrix = []
        for y in range(self.height):
            row = []
            for x in range(self.width):
                row.append(self.is_walkable(x, y))
            matrix.append(row)
        return matrix
    
    def get_bounds(self) -> Tuple[int, int]:
        """获取地图边界（宽度，高度）"""
        return (self.width, self.height)
    
    def __repr__(self) -> str:
        return f"MapData(width={self.width}, height={self.height}, tiles={self._tile_count})"


class _TileArrays:
    __slots__ = (
        "background",
        "middle",
        "object",
        "door_index",
        "door_offset",
        "anim_frame",
        "anim_tick",
        "area",
        "light",
    )

    def __init__(self, tile_count: int):
        self.background = array('H', [0]) * tile_count
        self.middle = array('H', [0]) * tile_count
        self.object = array('H', [0]) * tile_count
        self.door_index = array('B', [0]) * tile_count
        self.door_offset = array('B', [0]) * tile_count
        self.anim_frame = array('B', [0]) * tile_count
        self.anim_tick = array('B', [0]) * tile_count
        self.area = array('B', [0]) * tile_count
        self.light = array('B', [0]) * tile_count


class _MapTileSequence:
    def __init__(self, map_data: MapData):
        self._map_data = map_data

    def __len__(self) -> int:
        return self._map_data.get_tile_count()

    def __getitem__(self, index: int) -> MapTile:
        tile = self._map_data.get_tile_by_index(index)
        if tile is None:
            raise IndexError("tile index out of range")
        return tile

    def __iter__(self) -> Iterable[MapTile]:
        for index in range(self._map_data.get_tile_count()):
            tile = self._map_data.get_tile_by_index(index)
            if tile is not None:
                yield tile
