"""
地图画布组件

基于 QGraphicsView 实现可缩放、可平移的地图渲染画布。
"""

import math
import os
import time
from collections import OrderedDict
from typing import Optional
from PyQt5.QtCore import Qt, QRectF, QPointF, pyqtSignal, QTimer
from PyQt5.QtGui import QPainter, QColor, QPen, QPixmap, QBrush, QPolygonF
from PyQt5.QtWidgets import (
    QGraphicsView,
    QGraphicsScene,
    QGraphicsItem,
    QGraphicsPolygonItem,
    QGraphicsPixmapItem,
)

from tile_data import MapData, MapTile
from wil_loader import WILResourceManager


# 瓦片大小常量
TILE_WIDTH = 48
TILE_HEIGHT = 32
MAX_OBJECT_LIB = 39
OBJECT_BLEND_OFFSET_X = -2
OBJECT_BLEND_OFFSET_Y = -68
OBJECT_Z_ORDER_STEP = 0.001
OBJECT_LARGE_Z_OFFSET = 20.0


def _read_env_float(name: str, default: float, min_value: float, max_value: float) -> float:
    try:
        value = float(os.environ.get(name, default))
    except (TypeError, ValueError):
        value = default
    return max(min_value, min(max_value, value))


def _read_env_int(name: str, default: int, min_value: int, max_value: int) -> int:
    try:
        value = int(os.environ.get(name, default))
    except (TypeError, ValueError):
        value = default
    return max(min_value, min(max_value, value))


# Lower values keep detail longer; higher values reduce work earlier.
LOW_ZOOM_THRESHOLD = _read_env_float("MAP_VIEWER_LOW_ZOOM_THRESHOLD", 0.35, 0.2, 0.6)
CHUNK_TILE_SIZE = _read_env_int("MAP_VIEWER_CHUNK_TILE_SIZE", 16, 8, 64)
CHUNK_CACHE_SIZE = _read_env_int("MAP_VIEWER_CHUNK_CACHE_SIZE", 256, 64, 2048)
CHUNK_PREVIEW_CACHE_SIZE = _read_env_int("MAP_VIEWER_CHUNK_PREVIEW_CACHE_SIZE", 128, 32, 1024)
PREVIEW_SCALE = _read_env_float("MAP_VIEWER_PREVIEW_SCALE", 0.5, 0.25, 1.0)
MAX_OVERLAY_TILES = _read_env_int("MAP_VIEWER_MAX_OVERLAY_TILES", 1_000_000, 100_000, 5_000_000)
CHUNK_PADDING_TILES = _read_env_int("MAP_VIEWER_CHUNK_PADDING_TILES", 0, 0, 512)
CHUNK_PADDING_OBJECTS = _read_env_int("MAP_VIEWER_CHUNK_PADDING_OBJECTS", 128, 0, 1024)
ANIMATION_INTERVAL_MS = _read_env_int("MAP_VIEWER_ANIMATION_INTERVAL_MS", 100, 16, 1000)
USE_OBJECT_ITEMS = _read_env_int("MAP_VIEWER_OBJECT_ITEMS", 1, 0, 1) == 1
USE_LARGE_OBJECT_ITEMS = _read_env_int("MAP_VIEWER_LARGE_OBJECT_ITEMS", 1, 0, 1) == 1
LARGE_MAP_TILE_COUNT = _read_env_int("MAP_VIEWER_LARGE_MAP_TILES", 1_000_000, 100_000, 20_000_000)
LARGE_CHUNK_TILE_SIZE = _read_env_int("MAP_VIEWER_LARGE_CHUNK_TILE_SIZE", 64, 16, 128)
MAX_OBJECT_ITEMS = _read_env_int("MAP_VIEWER_MAX_OBJECT_ITEMS", 100_000, 1000, 5_000_000)
CHUNK_BUILD_BATCH = _read_env_int("MAP_VIEWER_CHUNK_BUILD_BATCH", 256, 16, 4096)
OBJECT_BUILD_BATCH = _read_env_int("MAP_VIEWER_OBJECT_BUILD_BATCH", 512, 16, 4096)
PROFILE_LOAD = _read_env_int("MAP_VIEWER_PROFILE", 0, 0, 1) == 1

LAYER_BACKGROUND = "background"
LAYER_MIDDLE = "middle"
LAYER_OBJECT = "object"
LAYER_PREVIEW = "preview"

# Explicit Z-order to avoid layer inversion (higher draws on top).
Z_BACKGROUND = 0.0
Z_MIDDLE = 10.0
Z_OBJECT = 20.0
Z_PREVIEW = 5.0


def _normalize_tile_index(value: int) -> Optional[int]:
    index = value & 0x7FFF
    if index <= 0:
        return None
    index -= 1
    return index if index >= 0 else None


def _normalize_object_index(value: int) -> Optional[int]:
    index = value & 0x7FFF
    if index <= 0:
        return None
    index -= 1
    return index if index >= 0 else None


def _object_archive_name(area: int) -> str:
    try:
        unit_index = int(area)
    except (TypeError, ValueError):
        unit_index = 0
    if unit_index < 0:
        unit_index = 0
    if unit_index >= MAX_OBJECT_LIB:
        unit_index = MAX_OBJECT_LIB - 1
    if unit_index <= 0:
        return "Objects"
    return f"Objects{unit_index + 1}"


def _resolve_object_frame(
    resource_manager: WILResourceManager,
    archive_name: str,
    obj_index: int,
    blend: bool,
) -> Optional[tuple[QPixmap, int, int, bool]]:
    if blend:
        frame_info = resource_manager.get_frame_with_offset(archive_name, obj_index)
        if frame_info is None and archive_name != "Objects":
            frame_info = resource_manager.get_frame_with_offset("Objects", obj_index)
        if not frame_info:
            return None
        pixmap, offset_x, offset_y = frame_info
        return (
            pixmap,
            offset_x + OBJECT_BLEND_OFFSET_X,
            offset_y + OBJECT_BLEND_OFFSET_Y,
            True,
        )

    frame = resource_manager.get_frame(archive_name, obj_index)
    if frame is None and archive_name != "Objects":
        frame = resource_manager.get_frame("Objects", obj_index)
    if not frame:
        return None
    return (frame, 0, TILE_HEIGHT - frame.height(), False)


def _object_is_large(pixmap: QPixmap, blend: bool) -> bool:
    return blend or pixmap.width() != TILE_WIDTH or pixmap.height() != TILE_HEIGHT


class MapTileItem(QGraphicsItem):
    """
    地图瓦片图形项
    
    渲染单个地图瓦片的三层图像。
    """
    
    def __init__(self, x: int, y: int, tile: MapTile, resource_manager: WILResourceManager):
        super().__init__()
        self.tile_x = x
        self.tile_y = y
        self.tile = tile
        self.resource_manager = resource_manager
        
        # 设置位置
        self.setPos(x * TILE_WIDTH, y * TILE_HEIGHT)
        
        # 图层可见性
        self.show_background = True
        self.show_middle = True
        self.show_object = True
        
        # 缓存边界
        self._bounding_rect = QRectF(0, 0, TILE_WIDTH, TILE_HEIGHT)
    
    def boundingRect(self):
        return self._bounding_rect
    
    def paint(self, painter, option, widget):
        """渲染瓦片"""
        # 渲染背景层
        bg_index = _normalize_tile_index(self.tile.background)
        if self.show_background and bg_index is not None:
            if (self.tile_x % 2 == 0) and (self.tile_y % 2 == 0):
                frame = self.resource_manager.get_frame("Tiles", bg_index)
            else:
                frame = None
            if frame:
                painter.drawPixmap(0, 0, frame)
        
        # 渲染中间层
        mid_index = _normalize_tile_index(self.tile.middle)
        if self.show_middle and mid_index is not None:
            frame = self.resource_manager.get_frame("SmTiles", mid_index)
            if frame:
                painter.drawPixmap(0, 0, frame)
        
        # 渲染物件层
        if self.show_object and self.tile.object > 0:
            obj_index = _normalize_object_index(self.tile.object)
            if obj_index is not None:
                archive_name = _object_archive_name(self.tile.area)
                blend = (self.tile.anim_frame & 0x80) != 0
                frame_info = _resolve_object_frame(
                    self.resource_manager,
                    archive_name,
                    obj_index,
                    blend,
                )
                if frame_info:
                    pixmap, offset_x, offset_y, _is_blend = frame_info
                    painter.drawPixmap(offset_x, offset_y, pixmap)
    
    def set_layer_visibility(self, show_bg: bool, show_mid: bool, show_obj: bool):
        """设置图层可见性"""
        self.show_background = show_bg
        self.show_middle = show_mid
        self.show_object = show_obj
        self.update()


class ObjectTileItem(QGraphicsPixmapItem):
    """Object layer item rendered per tile to avoid chunk clipping."""

    def __init__(self, tile_index: int, x: int, y: int, object_fields: tuple, canvas: "MapCanvas"):
        super().__init__()
        self.tile_index = tile_index
        self.tile_x = x
        self.tile_y = y
        self.canvas = canvas
        self._object_fields = object_fields
        self._pixmap_ready = False
        self.setZValue(Z_OBJECT)
        self.setPos(x * TILE_WIDTH, y * TILE_HEIGHT)

    def update_fields(self, object_fields: tuple) -> None:
        self._object_fields = object_fields
        self._pixmap_ready = False
        self.setVisible(True)

    def paint(self, painter, option, widget):
        if not self._pixmap_ready:
            self.refresh_pixmap()
        super().paint(painter, option, widget)

    def refresh_pixmap(self, force: bool = False) -> None:
        if self._pixmap_ready and not force:
            return
        self._pixmap_ready = True

        if not self._object_fields:
            self.setVisible(False)
            self.setPixmap(QPixmap())
            return

        obj_index, blend = self.canvas._compute_object_index(*self._object_fields[:5])
        if obj_index is None:
            self.setVisible(False)
            self.setPixmap(QPixmap())
            return

        area = self._object_fields[5] if len(self._object_fields) > 5 else 0
        archive_name = self.canvas._get_object_archive_name(area)
        frame_info = _resolve_object_frame(
            self.canvas.resource_manager,
            archive_name,
            obj_index,
            blend,
        )
        if not frame_info:
            self.setVisible(False)
            self.setPixmap(QPixmap())
            return
        pixmap, offset_x, offset_y, is_blend = frame_info
        self.setPixmap(pixmap)
        self.setOffset(offset_x, offset_y)
        is_large = _object_is_large(pixmap, is_blend)
        self.setZValue(self.canvas._object_z_value(self.tile_y, is_large))
        self.setVisible(True)

class ChunkPixmapCache:
    """Simple LRU cache for chunk pixmaps."""

    def __init__(self, max_items: int):
        self.max_items = max(1, max_items)
        self._items = OrderedDict()

    def get(self, key):
        pixmap = self._items.get(key)
        if pixmap is not None:
            self._items.move_to_end(key)
        return pixmap

    def put(self, key, pixmap):
        self._items[key] = pixmap
        self._items.move_to_end(key)
        while len(self._items) > self.max_items:
            self._items.popitem(last=False)

    def remove(self, key):
        self._items.pop(key, None)

    def clear(self):
        self._items.clear()


class ChunkLayerItem(QGraphicsItem):
    """Chunk-level renderer for a single layer."""

    def __init__(self, chunk_x: int, chunk_y: int, width_px: int, height_px: int,
                 layer: str, canvas: "MapCanvas", padding: int):
        super().__init__()
        self.chunk_x = chunk_x
        self.chunk_y = chunk_y
        self.layer = layer
        self.canvas = canvas
        self.base_width = width_px
        self.base_height = height_px
        self.padding = padding
        self._bounding_rect = QRectF(
            0, 0,
            width_px + padding * 2,
            height_px + padding * 2
        )

    def boundingRect(self):
        return self._bounding_rect

    def paint(self, painter, option, widget):
        pixmap = self.canvas.get_chunk_pixmap(self.layer, self.chunk_x, self.chunk_y)
        if not pixmap:
            return
        target = self.boundingRect()
        painter.drawPixmap(target, pixmap, QRectF(pixmap.rect()))


class MapCanvas(QGraphicsView):
    """
    地图画布
    
    可缩放、可平移的地图查看器。
    """
    
    # 信号
    tile_selected = pyqtSignal(int, int, object)  # (x, y, tile)
    tile_hovered = pyqtSignal(int, int)  # (x, y)
    zoom_changed = pyqtSignal(float)  # zoom_factor
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        # 创建场景
        self.scene = QGraphicsScene(self)
        self.setScene(self.scene)
        
        # 设置视图属性
        self.setRenderHint(QPainter.Antialiasing, True)
        self.setRenderHint(QPainter.SmoothPixmapTransform, True)
        self.setViewportUpdateMode(QGraphicsView.MinimalViewportUpdate)
        self.setOptimizationFlag(QGraphicsView.DontSavePainterState, True)
        self.setOptimizationFlag(QGraphicsView.DontAdjustForAntialiasing, True)
        self.setDragMode(QGraphicsView.NoDrag)
        self.setTransformationAnchor(QGraphicsView.AnchorUnderMouse)
        self.setResizeAnchor(QGraphicsView.AnchorUnderMouse)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOn)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOn)
        
        # 地图数据
        self.map_data: Optional[MapData] = None
        self.resource_manager = WILResourceManager()
        self.half_tile_width = TILE_WIDTH / 2.0
        self.half_tile_height = TILE_HEIGHT / 2.0
        self.iso_origin_x = 0.0
        self.iso_origin_y = 0.0
        
        # 区块渲染缓存
        self.chunk_tile_size = CHUNK_TILE_SIZE
        self.preview_scale = PREVIEW_SCALE
        self._chunk_cache = ChunkPixmapCache(CHUNK_CACHE_SIZE)
        self._preview_cache = ChunkPixmapCache(CHUNK_PREVIEW_CACHE_SIZE)
        self._layer_padding = {
            LAYER_BACKGROUND: CHUNK_PADDING_TILES,
            LAYER_MIDDLE: CHUNK_PADDING_TILES,
            LAYER_OBJECT: CHUNK_PADDING_OBJECTS,
            LAYER_PREVIEW: CHUNK_PADDING_OBJECTS,
        }
        self.chunk_items = {
            LAYER_BACKGROUND: {},
            LAYER_MIDDLE: {},
            LAYER_OBJECT: {},
        }
        self.preview_items = {}
        self.use_object_items = USE_OBJECT_ITEMS
        self.use_large_object_items = False
        self.object_items = {}
        self.animated_object_items = set()
        self._object_size_cache = {}
        
        # 显示选项
        self.show_grid = False
        self.show_walkability = False
        self.show_background = True
        self.show_middle = True
        self.show_object = True
        
        # 网格和可行走性覆盖层
        self.grid_items = []
        self.walkability_items = []
        
        # 缩放范围
        self.min_zoom = 0.1
        self.max_zoom = 5.0
        self.current_zoom = 1.0
        self.low_zoom_threshold = LOW_ZOOM_THRESHOLD
        self._low_zoom_active = False
        self._effective_layer_visibility = (None, None, None, None)
        self._effective_overlay_visibility = (None, None)
        
        # 选中的瓦片
        self.selected_tile_pos = None
        self.selection_rect: Optional[QGraphicsPolygonItem] = None

        # 动画状态
        self.main_ani_count = 0
        self._has_animated_tiles = False
        self._ani_timer = QTimer(self)
        self._ani_timer.setInterval(ANIMATION_INTERVAL_MS)
        self._ani_timer.timeout.connect(self._on_anim_tick)

        # Incremental build timers
        self._chunk_build_timer = QTimer(self)
        self._chunk_build_timer.setInterval(0)
        self._chunk_build_timer.timeout.connect(self._build_chunk_batch)
        self._chunk_build_queue = []
        self._chunk_build_cursor = 0
        self._object_build_timer = QTimer(self)
        self._object_build_timer.setInterval(0)
        self._object_build_timer.timeout.connect(self._build_object_batch)
        self._object_build_queue = []
        self._object_build_cursor = 0
        self._use_incremental_build = False
        
        # 拖拽状态
        self.dragging = False
        self.drag_start_pos = None
        self.last_drag_pos = None
        self.drag_threshold = 6
        self._pressed_left = False
    
    def load_map(self, map_data: MapData, data_directory: str = ""):
        """
        加载地图数据
        
        Args:
            map_data: 地图数据对象
            data_directory: Data 目录路径
        """
        load_start = time.perf_counter() if PROFILE_LOAD else None
        self.map_data = map_data
        self.main_ani_count = 0
        self._has_animated_tiles = map_data.get_has_animated_tiles()
        tile_count = map_data.get_tile_count()
        object_count = map_data.get_layer_count("object")
        self.chunk_tile_size = CHUNK_TILE_SIZE
        self._use_incremental_build = tile_count >= LARGE_MAP_TILE_COUNT
        if self._use_incremental_build:
            self.chunk_tile_size = max(self.chunk_tile_size, LARGE_CHUNK_TILE_SIZE)
        self.use_object_items = USE_OBJECT_ITEMS and object_count <= MAX_OBJECT_ITEMS
        self.use_large_object_items = (
            USE_OBJECT_ITEMS
            and USE_LARGE_OBJECT_ITEMS
            and not self.use_object_items
            and object_count > 0
        )
        if self._has_animated_tiles:
            if not self._ani_timer.isActive():
                self._ani_timer.start()
        else:
            self._ani_timer.stop()
        
        # 设置资源目录
        if data_directory:
            self.resource_manager.set_data_directory(data_directory)
        
        # 清空场景
        if self._chunk_build_timer.isActive():
            self._chunk_build_timer.stop()
        if self._object_build_timer.isActive():
            self._object_build_timer.stop()
        self.scene.clear()
        for items in self.chunk_items.values():
            items.clear()
        self.preview_items.clear()
        self._chunk_cache.clear()
        self._preview_cache.clear()
        self.grid_items.clear()
        self.walkability_items.clear()
        self.object_items.clear()
        self.animated_object_items.clear()
        self._object_size_cache.clear()
        self._chunk_build_queue = []
        self._chunk_build_cursor = 0
        self._object_build_queue = []
        self._object_build_cursor = 0
        self.selection_rect = None
        self._effective_layer_visibility = (None, None, None, None)
        self._effective_overlay_visibility = (None, None)

        self._update_iso_origin_and_scene()
        
        # 加载必要的 WIL 档案
        print("加载 WIL 资源...")
        self.resource_manager.load_archive("Tiles")
        self.resource_manager.load_archive("SmTiles")
        self.resource_manager.load_archive("Objects")
        
        # 创建区块项（支持渐进式加载）
        self._start_chunk_build()
        if self.use_object_items or self.use_large_object_items:
            self._start_object_build()
        
        # 创建网格/可行走性覆盖层（仅在启用时）
        if self.show_grid:
            self._create_grid()
        if self.show_walkability:
            self._create_walkability_overlay()
        
        # 居中显示
        scene_rect = self.scene.sceneRect()
        self.centerOn(scene_rect.width() / 2, scene_rect.height() / 2)
        
        # Apply zoom-dependent rendering options after items are created.
        if not self._use_incremental_build:
            self._apply_zoom_rendering_options()
        if PROFILE_LOAD and load_start is not None:
            elapsed = time.perf_counter() - load_start
            print(f"MapCanvas: load_map scheduled in {elapsed:.3f}s (tiles={tile_count})")
        print("地图加载完成!")

    def _update_iso_origin_and_scene(self):
        """Update scene bounds to match the game renderer's orthogonal grid."""
        if not self.map_data:
            return
        self.iso_origin_x = 0.0
        self.iso_origin_y = 0.0
        scene_w = max(1, int(self.map_data.width * TILE_WIDTH))
        scene_h = max(1, int(self.map_data.height * TILE_HEIGHT))
        self.scene.setSceneRect(0, 0, scene_w, scene_h)

    def _tile_to_scene(self, tile_x: int, tile_y: int) -> QPointF:
        sx = tile_x * TILE_WIDTH + self.iso_origin_x
        sy = tile_y * TILE_HEIGHT + self.iso_origin_y
        return QPointF(sx, sy)

    def _scene_to_tile(self, scene_pos: QPointF) -> tuple[int, int]:
        sx = scene_pos.x() - self.iso_origin_x
        sy = scene_pos.y() - self.iso_origin_y
        if TILE_WIDTH == 0 or TILE_HEIGHT == 0:
            return -1, -1
        tile_x = math.floor(sx / TILE_WIDTH)
        tile_y = math.floor(sy / TILE_HEIGHT)
        return tile_x, tile_y

    def _pick_tile_at_scene_pos(self, scene_pos: QPointF) -> tuple[int, int]:
        base_x, base_y = self._scene_to_tile(scene_pos)
        candidates = [
            (base_x, base_y),
            (base_x - 1, base_y),
            (base_x + 1, base_y),
            (base_x, base_y - 1),
            (base_x, base_y + 1),
        ]
        for x, y in candidates:
            if not self.map_data or not self.map_data.is_valid_position(x, y):
                continue
            polygon = self._tile_polygon(x, y)
            if polygon.containsPoint(scene_pos, Qt.OddEvenFill):
                return x, y
        return base_x, base_y

    def _tile_polygon(self, tile_x: int, tile_y: int) -> QPolygonF:
        top_left = self._tile_to_scene(tile_x, tile_y)
        cx = top_left.x()
        cy = top_left.y()
        return QPolygonF([
            QPointF(cx, cy),
            QPointF(cx + TILE_WIDTH, cy),
            QPointF(cx + TILE_WIDTH, cy + TILE_HEIGHT),
            QPointF(cx, cy + TILE_HEIGHT),
        ])

    def _get_object_archive_name(self, area: int) -> str:
        return _object_archive_name(area)

    def _compute_anim_info(self, anim_frame: int, anim_tick: int) -> tuple[int, bool]:
        blend = (anim_frame & 0x80) != 0
        frames = anim_frame & 0x7F
        if frames <= 0:
            return 0, blend
        tick = max(0, anim_tick)
        cycle = frames * (1 + tick)
        if cycle <= 0:
            return 0, blend
        offset = (self.main_ani_count % cycle) // (1 + tick)
        return offset, blend

    def _compute_object_index(
        self,
        object_index: int,
        door_index: int,
        door_offset: int,
        anim_frame: int,
        anim_tick: int,
    ) -> tuple[Optional[int], bool]:
        base_index = object_index & 0x7FFF
        frame_offset, blend = self._compute_anim_info(anim_frame, anim_tick)
        if base_index <= 0:
            return None, blend
        base_index += frame_offset
        if (door_offset & 0x80) != 0 and (door_index & 0x7F) > 0:
            base_index += (door_offset & 0x7F)
        if base_index <= 0:
            return None, blend
        return base_index - 1, blend

    def _object_tile_has_sprite(
        self,
        object_index: int,
        door_index: int,
        door_offset: int,
        anim_frame: int,
    ) -> bool:
        base_index = object_index & 0x7FFF
        if base_index <= 0:
            return False
        if (door_offset & 0x80) != 0 and (door_index & 0x7F) > 0:
            base_index += (door_offset & 0x7F)
        return base_index > 0

    def _get_object_frame_size(self, archive_name: str, obj_index: int) -> Optional[tuple[int, int]]:
        key = (archive_name, obj_index)
        if key in self._object_size_cache:
            return self._object_size_cache[key]

        header = self.resource_manager.get_frame_header(archive_name, obj_index)
        if header is None and archive_name != "Objects":
            header = self.resource_manager.get_frame_header("Objects", obj_index)
        if not header:
            self._object_size_cache[key] = None
            return None
        width, height, _offset_x, _offset_y = header
        size = (width, height)
        self._object_size_cache[key] = size
        return size

    def _object_tile_needs_item(self, object_fields: tuple) -> bool:
        obj_index, blend = self._compute_object_index(*object_fields[:5])
        if obj_index is None:
            return False
        if self.use_object_items:
            return True
        if not self.use_large_object_items:
            return False
        if blend:
            return True
        area = object_fields[5] if len(object_fields) > 5 else 0
        archive_name = self._get_object_archive_name(area)
        size = self._get_object_frame_size(archive_name, obj_index)
        if not size:
            return False
        return size[0] != TILE_WIDTH or size[1] != TILE_HEIGHT

    def _object_z_value(self, tile_y: int, is_large: bool) -> float:
        base = Z_OBJECT + (OBJECT_LARGE_Z_OFFSET if is_large else 0.0)
        return base + (tile_y * OBJECT_Z_ORDER_STEP)

    def _on_anim_tick(self):
        if not self.map_data or not self._has_animated_tiles:
            return
        self.main_ani_count = (self.main_ani_count + 1) % 1000000
        if self.use_object_items or self.use_large_object_items:
            for item in self.animated_object_items:
                item.refresh_pixmap(force=True)
            if self.use_object_items:
                return
        for key in list(self._chunk_cache._items.keys()):
            if key[0] == LAYER_OBJECT:
                self._chunk_cache.remove(key)
        self._preview_cache.clear()
        for item in self.chunk_items[LAYER_OBJECT].values():
            item.update()
        for item in self.preview_items.values():
            item.update()
    
    def _create_grid(self):
        """创建网格线"""
        if not self.map_data:
            return

        for item in self.grid_items:
            self.scene.removeItem(item)
        self.grid_items.clear()

        if self.map_data.get_tile_count() > MAX_OVERLAY_TILES:
            print(
                "警告: 地图过大，跳过网格显示 "
                f"({self.map_data.get_tile_count()} > {MAX_OVERLAY_TILES})"
            )
            return
        
        pen = QPen(QColor(100, 100, 100, 128))
        pen.setWidth(0)  # 使用化妆品笔宽（不随缩放变化）

        for y in range(self.map_data.height):
            for x in range(self.map_data.width):
                polygon = self._tile_polygon(x, y)
                item = self.scene.addPolygon(polygon, pen)
                item.setVisible(self.show_grid)
                item.setZValue(1000)
                self.grid_items.append(item)
    
    def _create_walkability_overlay(self):
        """创建可行走性覆盖层"""
        if not self.map_data:
            return

        if self.map_data.get_tile_count() > MAX_OVERLAY_TILES:
            print(
                "警告: 地图过大，跳过可行走性覆盖层 "
                f"({self.map_data.get_tile_count()} > {MAX_OVERLAY_TILES})"
            )
            return

        for item in self.walkability_items:
            self.scene.removeItem(item)
        self.walkability_items.clear()
        
        walkable_brush = QBrush(QColor(0, 255, 0, 64))
        blocked_brush = QBrush(QColor(255, 0, 0, 64))
        pen = QPen(Qt.NoPen)
        
        for y in range(self.map_data.height):
            for x in range(self.map_data.width):
                brush = walkable_brush if self.map_data.is_walkable(x, y) else blocked_brush
                polygon = self._tile_polygon(x, y)
                item = self.scene.addPolygon(polygon, pen, brush)
                item.setVisible(self.show_walkability)
                item.setZValue(999)  # 在网格下方
                self.walkability_items.append(item)

    def _start_chunk_build(self):
        if not self.map_data:
            return
        chunks_x = (self.map_data.width + self.chunk_tile_size - 1) // self.chunk_tile_size
        chunks_y = (self.map_data.height + self.chunk_tile_size - 1) // self.chunk_tile_size
        print(f"创建区块项 ({chunks_x}x{chunks_y})...")
        self._chunk_build_queue = [(cx, cy) for cy in range(chunks_y) for cx in range(chunks_x)]
        self._chunk_build_cursor = 0
        if self._use_incremental_build:
            self._chunk_build_timer.start()
        else:
            self._build_chunk_batch(len(self._chunk_build_queue))

    def _build_chunk_items(self, chunk_x: int, chunk_y: int):
        min_x, min_y, width_px, height_px = self._get_chunk_scene_bounds(chunk_x, chunk_y)
        if width_px <= 0 or height_px <= 0:
            return

        bg_visible, mid_visible, obj_visible, preview_visible = self._current_layer_visibility()

        for layer, z_value in (
            (LAYER_BACKGROUND, Z_BACKGROUND),
            (LAYER_MIDDLE, Z_MIDDLE),
            (LAYER_OBJECT, Z_OBJECT),
        ):
            if layer == LAYER_OBJECT and self.use_object_items:
                continue
            padding = self._layer_padding[layer]
            item = ChunkLayerItem(chunk_x, chunk_y, width_px, height_px, layer, self, padding)
            item.setPos(min_x - padding, min_y - padding)
            item.setZValue(z_value)
            if layer == LAYER_BACKGROUND:
                item.setVisible(bg_visible)
            elif layer == LAYER_MIDDLE:
                item.setVisible(mid_visible)
            else:
                item.setVisible(obj_visible)
            self.scene.addItem(item)
            self.chunk_items[layer][(chunk_x, chunk_y)] = item

        preview_padding = self._layer_padding[LAYER_PREVIEW]
        preview_item = ChunkLayerItem(
            chunk_x, chunk_y, width_px, height_px, LAYER_PREVIEW, self, preview_padding
        )
        preview_item.setPos(min_x - preview_padding, min_y - preview_padding)
        preview_item.setZValue(Z_PREVIEW)
        preview_item.setVisible(preview_visible)
        self.scene.addItem(preview_item)
        self.preview_items[(chunk_x, chunk_y)] = preview_item

    def _build_chunk_batch(self, batch_size: Optional[int] = None):
        if not self._chunk_build_queue:
            return
        batch = CHUNK_BUILD_BATCH if batch_size is None else batch_size
        end = min(self._chunk_build_cursor + batch, len(self._chunk_build_queue))
        for chunk_x, chunk_y in self._chunk_build_queue[self._chunk_build_cursor:end]:
            self._build_chunk_items(chunk_x, chunk_y)
        self._chunk_build_cursor = end
        if self._chunk_build_cursor >= len(self._chunk_build_queue):
            self._chunk_build_timer.stop()
            self._chunk_build_queue = []
            self._chunk_build_cursor = 0
            self._apply_zoom_rendering_options()

    def _start_object_build(self):
        if not self.map_data or not (self.use_object_items or self.use_large_object_items):
            return
        objects = self.map_data._object
        door_index = self.map_data._door_index
        door_offset = self.map_data._door_offset
        anim_frame = self.map_data._anim_frame
        anim_tick = self.map_data._anim_tick
        area = self.map_data._area
        has_sprite = self._object_tile_has_sprite
        self._object_build_queue = []
        for index in range(self.map_data.get_tile_count()):
            if not has_sprite(
                objects[index],
                door_index[index],
                door_offset[index],
                anim_frame[index],
            ):
                continue
            object_fields = (
                objects[index],
                door_index[index],
                door_offset[index],
                anim_frame[index],
                anim_tick[index],
                area[index],
            )
            if self._object_tile_needs_item(object_fields):
                self._object_build_queue.append(index)
        self._object_build_cursor = 0
        if self._use_incremental_build or len(self._object_build_queue) > OBJECT_BUILD_BATCH:
            self._object_build_timer.start()
        else:
            self._build_object_batch(len(self._object_build_queue))

    def _build_object_batch(self, batch_size: Optional[int] = None):
        if not self._object_build_queue or not self.map_data:
            return
        batch = OBJECT_BUILD_BATCH if batch_size is None else batch_size
        end = min(self._object_build_cursor + batch, len(self._object_build_queue))
        for index in self._object_build_queue[self._object_build_cursor:end]:
            self._build_object_item_for_index(index)
        self._object_build_cursor = end
        if self._object_build_cursor >= len(self._object_build_queue):
            self._object_build_timer.stop()
            self._object_build_queue = []
            self._object_build_cursor = 0

    def _build_object_item_for_index(self, index: int):
        if not self.map_data:
            return
        object_fields = self.map_data.get_object_fields_by_index(index)
        if not object_fields:
            return
        if not self._object_tile_needs_item(object_fields):
            return
        x = index % self.map_data.width
        y = index // self.map_data.width
        item = ObjectTileItem(index, x, y, object_fields, self)
        item.setVisible(self._current_layer_visibility()[2])
        self.scene.addItem(item)
        self.object_items[(x, y)] = item
        if (object_fields[3] & 0x7F) > 0:
            self.animated_object_items.add(item)

    def _refresh_object_item(self, x: int, y: int):
        if not self.map_data or not (self.use_object_items or self.use_large_object_items):
            return
        index = y * self.map_data.width + x
        object_fields = self.map_data.get_object_fields_by_index(index)
        item = self.object_items.get((x, y))

        needs_item = object_fields and self._object_tile_needs_item(object_fields)
        if not needs_item:
            if item:
                self.scene.removeItem(item)
                self.object_items.pop((x, y), None)
                self.animated_object_items.discard(item)
            return

        if not item:
            item = ObjectTileItem(index, x, y, object_fields, self)
            self.scene.addItem(item)
            self.object_items[(x, y)] = item
        else:
            item.update_fields(object_fields)

        if (object_fields[3] & 0x7F) > 0:
            self.animated_object_items.add(item)
        else:
            self.animated_object_items.discard(item)

        item.setVisible(self._current_layer_visibility()[2])

    def _set_object_items_visible(self, visible: bool):
        if not (self.use_object_items or self.use_large_object_items):
            return
        for item in self.object_items.values():
            item.setVisible(visible)

    def _current_layer_visibility(self) -> tuple[bool, bool, bool, bool]:
        low_zoom = self.current_zoom <= self.low_zoom_threshold
        preview_visible = low_zoom and (self.show_background or self.show_middle or self.show_object)
        if low_zoom:
            return False, False, False, preview_visible
        return self.show_background, self.show_middle, self.show_object, False

    def set_grid_visible(self, visible: bool):
        """设置网格可见性"""
        self.show_grid = visible
        if visible and not self.grid_items:
            self._create_grid()
        self._apply_zoom_rendering_options()
    
    def set_walkability_visible(self, visible: bool):
        """设置可行走性覆盖层可见性"""
        self.show_walkability = visible
        if visible and not self.walkability_items:
            self._create_walkability_overlay()
        self._apply_zoom_rendering_options()
    
    def set_layer_visibility(self, show_bg: bool, show_mid: bool, show_obj: bool):
        """设置图层可见性"""
        self.show_background = show_bg
        self.show_middle = show_mid
        self.show_object = show_obj
        self._preview_cache.clear()
        for item in self.preview_items.values():
            item.update()
        self._set_object_items_visible(show_obj)
        self._apply_zoom_rendering_options()
    
    def set_zoom(self, zoom_factor: float):
        """设置缩放级别"""
        zoom_factor = max(self.min_zoom, min(self.max_zoom, zoom_factor))
        
        # 计算缩放比例
        scale_factor = zoom_factor / self.current_zoom
        
        # 应用缩放
        self.scale(scale_factor, scale_factor)
        
        self.current_zoom = zoom_factor
        self.zoom_changed.emit(zoom_factor)
        self._apply_zoom_rendering_options()

    def _apply_zoom_rendering_options(self):
        """Adjust rendering quality and layer visibility based on zoom."""
        if not self.map_data:
            return

        low_zoom = self.current_zoom <= self.low_zoom_threshold
        if low_zoom != self._low_zoom_active:
            self.setRenderHint(QPainter.Antialiasing, not low_zoom)
            self.setRenderHint(QPainter.SmoothPixmapTransform, not low_zoom)
            self._low_zoom_active = low_zoom

        preview_visible = low_zoom and (self.show_background or self.show_middle or self.show_object)
        if low_zoom:
            effective_layers = (False, False, False, preview_visible)
        else:
            effective_layers = (self.show_background, self.show_middle, self.show_object, False)

        if effective_layers != self._effective_layer_visibility:
            bg_visible, mid_visible, obj_visible, preview_visible = effective_layers
            for item in self.chunk_items[LAYER_BACKGROUND].values():
                item.setVisible(bg_visible)
            for item in self.chunk_items[LAYER_MIDDLE].values():
                item.setVisible(mid_visible)
            if not self.use_object_items:
                for item in self.chunk_items[LAYER_OBJECT].values():
                    item.setVisible(obj_visible)
            self._set_object_items_visible(obj_visible)
            for item in self.preview_items.values():
                item.setVisible(preview_visible)
            self._effective_layer_visibility = effective_layers

        effective_overlays = (
            self.show_grid and not low_zoom,
            self.show_walkability and not low_zoom,
        )
        if effective_overlays != self._effective_overlay_visibility:
            grid_visible, walk_visible = effective_overlays
            for item in self.grid_items:
                item.setVisible(grid_visible)
            for item in self.walkability_items:
                item.setVisible(walk_visible)
            self._effective_overlay_visibility = effective_overlays

    def _get_chunk_bounds(self, chunk_x: int, chunk_y: int):
        start_x = chunk_x * self.chunk_tile_size
        start_y = chunk_y * self.chunk_tile_size
        end_x = min(start_x + self.chunk_tile_size, self.map_data.width)
        end_y = min(start_y + self.chunk_tile_size, self.map_data.height)
        return start_x, start_y, end_x, end_y

    def _get_chunk_scene_bounds(self, chunk_x: int, chunk_y: int):
        start_x, start_y, end_x, end_y = self._get_chunk_bounds(chunk_x, chunk_y)
        min_x = start_x * TILE_WIDTH + self.iso_origin_x
        min_y = start_y * TILE_HEIGHT + self.iso_origin_y
        width = (end_x - start_x) * TILE_WIDTH
        height = (end_y - start_y) * TILE_HEIGHT
        return min_x, min_y, width, height

    def _render_object_pass(
        self,
        painter: QPainter,
        start_x: int,
        start_y: int,
        end_x: int,
        end_y: int,
        min_x: float,
        min_y: float,
        padding: float,
        want_large: bool,
    ) -> None:
        if not self.map_data:
            return
        for y in range(start_y, end_y):
            for x in range(start_x, end_x):
                fields = self.map_data.get_tile_fields(x, y)
                if not fields:
                    continue
                _background, _middle, object_index, door_index, door_offset, anim_frame, anim_tick, area, _light = fields
                obj_index, blend = self._compute_object_index(
                    object_index, door_index, door_offset, anim_frame, anim_tick
                )
                if obj_index is None:
                    continue
                archive_name = self._get_object_archive_name(area)
                if self.use_large_object_items:
                    size_hint = True if blend else None
                    if size_hint is None:
                        size = self._get_object_frame_size(archive_name, obj_index)
                        if size:
                            size_hint = size[0] != TILE_WIDTH or size[1] != TILE_HEIGHT
                    if size_hint is not None and want_large != size_hint:
                        continue
                frame_info = _resolve_object_frame(
                    self.resource_manager,
                    archive_name,
                    obj_index,
                    blend,
                )
                if not frame_info:
                    continue
                pixmap, offset_x, offset_y, is_blend = frame_info
                is_large = _object_is_large(pixmap, is_blend)
                if want_large != is_large:
                    continue
                tile_pos = self._tile_to_scene(x, y)
                draw_x = tile_pos.x() - min_x + padding + offset_x
                draw_y = tile_pos.y() - min_y + padding + offset_y
                painter.drawPixmap(int(round(draw_x)), int(round(draw_y)), pixmap)

    def get_chunk_pixmap(self, layer: str, chunk_x: int, chunk_y: int) -> Optional[QPixmap]:
        if not self.map_data:
            return None
        cache = self._preview_cache if layer == LAYER_PREVIEW else self._chunk_cache
        key = (layer, chunk_x, chunk_y)
        pixmap = cache.get(key)
        if pixmap is not None:
            return pixmap

        if layer == LAYER_PREVIEW:
            pixmap = self._render_chunk_preview(chunk_x, chunk_y)
        else:
            pixmap = self._render_chunk_layer(layer, chunk_x, chunk_y)

        if pixmap and not pixmap.isNull():
            cache.put(key, pixmap)
            return pixmap
        return None

    def _render_chunk_layer(self, layer: str, chunk_x: int, chunk_y: int) -> Optional[QPixmap]:
        if layer == LAYER_OBJECT and self.use_object_items:
            return None
        start_x, start_y, end_x, end_y = self._get_chunk_bounds(chunk_x, chunk_y)
        min_x, min_y, width_px, height_px = self._get_chunk_scene_bounds(chunk_x, chunk_y)
        if width_px <= 0 or height_px <= 0:
            return None
        padding = self._layer_padding.get(layer, 0)
        padded_width = width_px + padding * 2
        padded_height = height_px + padding * 2

        pixmap = QPixmap(padded_width, padded_height)
        pixmap.fill(Qt.transparent)
        painter = QPainter(pixmap)
        painter.setRenderHint(QPainter.SmoothPixmapTransform, False)

        if layer == LAYER_OBJECT:
            self._render_object_pass(
                painter,
                start_x,
                start_y,
                end_x,
                end_y,
                min_x,
                min_y,
                padding,
                False,
            )
            if not self.use_large_object_items:
                self._render_object_pass(
                    painter,
                    start_x,
                    start_y,
                    end_x,
                    end_y,
                    min_x,
                    min_y,
                    padding,
                    True,
                )
            painter.end()
            return pixmap

        for y in range(start_y, end_y):
            for x in range(start_x, end_x):
                fields = self.map_data.get_tile_fields(x, y)
                if not fields:
                    continue
                background, middle, object_index, door_index, door_offset, anim_frame, anim_tick, area, _light = fields
                tile_pos = self._tile_to_scene(x, y)
                draw_x = tile_pos.x() - min_x + padding
                draw_y = tile_pos.y() - min_y + padding
                if layer == LAYER_BACKGROUND:
                    if (x % 2 != 0) or (y % 2 != 0):
                        continue
                    bg_index = _normalize_tile_index(background)
                    if bg_index is None:
                        continue
                    frame = self.resource_manager.get_frame("Tiles", bg_index)
                    if frame:
                        painter.drawPixmap(
                            int(round(draw_x)),
                            int(round(draw_y)),
                            frame
                        )
                elif layer == LAYER_MIDDLE:
                    mid_index = _normalize_tile_index(middle)
                    if mid_index is None:
                        continue
                    frame = self.resource_manager.get_frame("SmTiles", mid_index)
                    if frame:
                        painter.drawPixmap(
                            int(round(draw_x)),
                            int(round(draw_y)),
                            frame
                        )

        painter.end()
        return pixmap

    def _render_chunk_preview(self, chunk_x: int, chunk_y: int) -> Optional[QPixmap]:
        start_x, start_y, end_x, end_y = self._get_chunk_bounds(chunk_x, chunk_y)
        min_x, min_y, width_px, height_px = self._get_chunk_scene_bounds(chunk_x, chunk_y)
        if width_px <= 0 or height_px <= 0:
            return None

        padding = self._layer_padding.get(LAYER_PREVIEW, 0)
        padded_width = width_px + padding * 2
        padded_height = height_px + padding * 2

        scale = self.preview_scale
        preview_w = max(1, int(padded_width * scale))
        preview_h = max(1, int(padded_height * scale))
        pixmap = QPixmap(preview_w, preview_h)
        pixmap.fill(Qt.transparent)

        painter = QPainter(pixmap)
        painter.setRenderHint(QPainter.SmoothPixmapTransform, False)
        if scale != 1.0:
            painter.scale(scale, scale)

        for y in range(start_y, end_y):
            for x in range(start_x, end_x):
                fields = self.map_data.get_tile_fields(x, y)
                if not fields:
                    continue
                background, middle, object_index, door_index, door_offset, anim_frame, anim_tick, area, _light = fields
                tile_pos = self._tile_to_scene(x, y)
                draw_x = tile_pos.x() - min_x + padding
                draw_y = tile_pos.y() - min_y + padding
                if self.show_background:
                    if (x % 2 == 0) and (y % 2 == 0):
                        bg_index = _normalize_tile_index(background)
                    else:
                        bg_index = None
                    if bg_index is not None:
                        frame = self.resource_manager.get_frame("Tiles", bg_index)
                        if frame:
                            painter.drawPixmap(
                                int(round(draw_x)),
                                int(round(draw_y)),
                                frame
                            )
                if self.show_middle:
                    mid_index = _normalize_tile_index(middle)
                    if mid_index is not None:
                        frame = self.resource_manager.get_frame("SmTiles", mid_index)
                        if frame:
                            painter.drawPixmap(
                                int(round(draw_x)),
                                int(round(draw_y)),
                                frame
                            )

        if self.show_object:
            self._render_object_pass(
                painter,
                start_x,
                start_y,
                end_x,
                end_y,
                min_x,
                min_y,
                padding,
                False,
            )
            self._render_object_pass(
                painter,
                start_x,
                start_y,
                end_x,
                end_y,
                min_x,
                min_y,
                padding,
                True,
            )

        painter.end()
        return pixmap
    
    def zoom_in(self):
        """放大"""
        self.set_zoom(self.current_zoom * 1.2)
    
    def zoom_out(self):
        """缩小"""
        self.set_zoom(self.current_zoom / 1.2)
    
    def reset_zoom(self):
        """重置缩放"""
        self.set_zoom(1.0)
    
    def wheelEvent(self, event):
        """鼠标滚轮事件 - 缩放"""
        if event.angleDelta().y() > 0:
            self.zoom_in()
        else:
            self.zoom_out()
    
    def mousePressEvent(self, event):
        """鼠标按下事件"""
        if event.button() == Qt.LeftButton and self.map_data:
            self._pressed_left = True
            self.dragging = False
            self.drag_start_pos = event.pos()
            self.last_drag_pos = event.pos()
            event.accept()
            return
        
        # 其他情况使用默认行为
        super().mousePressEvent(event)
    
    def mouseMoveEvent(self, event):
        """鼠标移动事件"""
        if self._pressed_left and self.map_data:
            if not self.dragging:
                if (event.pos() - self.drag_start_pos).manhattanLength() >= self.drag_threshold:
                    self.dragging = True
                    self.setCursor(Qt.ClosedHandCursor)
            if self.dragging:
                delta = event.pos() - self.last_drag_pos
                self.last_drag_pos = event.pos()
                self._pan_view(delta)
                event.accept()
                return

        if self.map_data:
            # 获取场景坐标
            scene_pos = self.mapToScene(event.pos())
            
            # 转换为瓦片坐标
            tile_x, tile_y = self._pick_tile_at_scene_pos(scene_pos)
            
            # 发送悬停信号
            if self.map_data.is_valid_position(tile_x, tile_y):
                self.tile_hovered.emit(tile_x, tile_y)
        
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        """鼠标释放事件"""
        if event.button() == Qt.LeftButton:
            if self._pressed_left and not self.dragging:
                self._select_tile_at_view_pos(event.pos())
            self._pressed_left = False
            self.dragging = False
            self.drag_start_pos = None
            self.last_drag_pos = None
            self.unsetCursor()
            event.accept()
            return

        super().mouseReleaseEvent(event)

    def _pan_view(self, delta):
        """Pan view by the given delta in viewport pixels."""
        if delta.isNull():
            return
        hbar = self.horizontalScrollBar()
        vbar = self.verticalScrollBar()
        hbar.setValue(hbar.value() - delta.x())
        vbar.setValue(vbar.value() - delta.y())

    def _select_tile_at_view_pos(self, view_pos):
        """Select tile at viewport position."""
        if not self.map_data:
            return
        scene_pos = self.mapToScene(view_pos)
        tile_x, tile_y = self._pick_tile_at_scene_pos(scene_pos)
        if self.map_data.is_valid_position(tile_x, tile_y):
            self.select_tile(tile_x, tile_y)

    def invalidate_tile(self, x: int, y: int):
        """Invalidate cached chunk(s) that contain the specified tile."""
        if not self.map_data or not self.map_data.is_valid_position(x, y):
            return
        chunk_x = x // self.chunk_tile_size
        chunk_y = y // self.chunk_tile_size
        for layer in (LAYER_BACKGROUND, LAYER_MIDDLE, LAYER_OBJECT):
            self._chunk_cache.remove((layer, chunk_x, chunk_y))
            item = self.chunk_items[layer].get((chunk_x, chunk_y))
            if item:
                item.update()
        self._preview_cache.remove((LAYER_PREVIEW, chunk_x, chunk_y))
        preview_item = self.preview_items.get((chunk_x, chunk_y))
        if preview_item:
            preview_item.update()
        if self.use_object_items or self.use_large_object_items:
            self._refresh_object_item(x, y)
    
    def select_tile(self, x: int, y: int):
        """
        选择瓦片
        
        Args:
            x: 瓦片X坐标
            y: 瓦片Y坐标
        """
        if not self.map_data or not self.map_data.is_valid_position(x, y):
            return
        
        self.selected_tile_pos = (x, y)
        
        # 移除旧的选择框
        if self.selection_rect:
            self.scene.removeItem(self.selection_rect)
        
        # 创建新的选择框
        pen = QPen(QColor(255, 255, 0))
        pen.setWidth(0)
        pen.setStyle(Qt.DashLine)
        polygon = self._tile_polygon(x, y)
        self.selection_rect = self.scene.addPolygon(polygon, pen)
        self.selection_rect.setZValue(1001)  # 最顶层
        
        # 发送选择信号
        tile = self.map_data.get_tile(x, y)
        self.tile_selected.emit(x, y, tile)
    
    def get_selected_tile(self) -> Optional[tuple]:
        """获取选中的瓦片坐标"""
        return self.selected_tile_pos
