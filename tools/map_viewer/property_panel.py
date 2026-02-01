"""
属性查看/编辑面板

显示和编辑选中瓦片的属性。
"""

from typing import Optional
from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QFormLayout,
    QLabel, QLineEdit, QSpinBox, QCheckBox, QPushButton,
    QTextEdit, QScrollArea
)

from tile_data import MapTile, MapData


class PropertyPanel(QWidget):
    """
    属性面板
    
    显示地图基本信息和选中瓦片的详细属性，支持编辑。
    """
    
    # 信号
    tile_modified = pyqtSignal(int, int, object)  # (x, y, tile)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        
        self.map_data: Optional[MapData] = None
        self.current_tile: Optional[MapTile] = None
        self.current_pos: Optional[tuple] = None
        
        self.init_ui()
    
    def init_ui(self):
        """初始化用户界面"""
        # 主布局
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(5, 5, 5, 5)
        
        # 创建滚动区域
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        
        # 内容容器
        content = QWidget()
        layout = QVBoxLayout(content)
        layout.setContentsMargins(0, 0, 0, 0)
        
        # 地图信息组
        map_group = QGroupBox("地图信息")
        map_layout = QFormLayout(map_group)
        
        self.map_size_label = QLabel("未加载")
        map_layout.addRow("尺寸:", self.map_size_label)
        
        self.tile_count_label = QLabel("0")
        map_layout.addRow("瓦片总数:", self.tile_count_label)
        
        self.walkable_count_label = QLabel("0")
        map_layout.addRow("可行走:", self.walkable_count_label)
        
        layout.addWidget(map_group)
        
        # 瓦片信息组
        tile_group = QGroupBox("瓦片信息")
        tile_layout = QFormLayout(tile_group)
        
        self.tile_pos_label = QLabel("未选择")
        tile_layout.addRow("坐标:", self.tile_pos_label)
        
        self.tile_walkable_label = QLabel("-")
        tile_layout.addRow("可行走:", self.tile_walkable_label)
        
        self.tile_portal_label = QLabel("-")
        tile_layout.addRow("传送门:", self.tile_portal_label)
        
        layout.addWidget(tile_group)
        
        # 图层索引组
        layer_group = QGroupBox("图层索引")
        layer_layout = QFormLayout(layer_group)
        
        self.bg_spin = QSpinBox()
        self.bg_spin.setRange(0, 65535)
        self.bg_spin.valueChanged.connect(self._on_property_changed)
        layer_layout.addRow("背景层:", self.bg_spin)
        
        self.mid_spin = QSpinBox()
        self.mid_spin.setRange(0, 65535)
        self.mid_spin.valueChanged.connect(self._on_property_changed)
        layer_layout.addRow("中间层:", self.mid_spin)
        
        self.obj_spin = QSpinBox()
        self.obj_spin.setRange(0, 65535)
        self.obj_spin.valueChanged.connect(self._on_property_changed)
        layer_layout.addRow("物件层:", self.obj_spin)
        
        layout.addWidget(layer_group)
        
        # 瓦片属性组
        attr_group = QGroupBox("瓦片属性")
        attr_layout = QFormLayout(attr_group)
        
        self.door_index_spin = QSpinBox()
        self.door_index_spin.setRange(0, 255)
        self.door_index_spin.valueChanged.connect(self._on_property_changed)
        attr_layout.addRow("门索引:", self.door_index_spin)
        
        self.door_offset_spin = QSpinBox()
        self.door_offset_spin.setRange(0, 255)
        self.door_offset_spin.valueChanged.connect(self._on_property_changed)
        attr_layout.addRow("门偏移:", self.door_offset_spin)
        
        self.anim_frame_spin = QSpinBox()
        self.anim_frame_spin.setRange(0, 255)
        self.anim_frame_spin.valueChanged.connect(self._on_property_changed)
        attr_layout.addRow("动画帧数:", self.anim_frame_spin)
        
        self.anim_tick_spin = QSpinBox()
        self.anim_tick_spin.setRange(0, 255)
        self.anim_tick_spin.valueChanged.connect(self._on_property_changed)
        attr_layout.addRow("动画速度:", self.anim_tick_spin)
        
        self.area_spin = QSpinBox()
        self.area_spin.setRange(0, 255)
        self.area_spin.valueChanged.connect(self._on_property_changed)
        attr_layout.addRow("区域标识:", self.area_spin)
        
        self.light_spin = QSpinBox()
        self.light_spin.setRange(0, 255)
        self.light_spin.valueChanged.connect(self._on_property_changed)
        attr_layout.addRow("光照:", self.light_spin)
        
        layout.addWidget(attr_group)
        
        # 操作按钮
        button_layout = QHBoxLayout()
        
        self.apply_btn = QPushButton("应用修改")
        self.apply_btn.setEnabled(False)
        self.apply_btn.clicked.connect(self._apply_changes)
        button_layout.addWidget(self.apply_btn)
        
        self.reset_btn = QPushButton("重置")
        self.reset_btn.setEnabled(False)
        self.reset_btn.clicked.connect(self._reset_values)
        button_layout.addWidget(self.reset_btn)
        
        layout.addLayout(button_layout)
        
        # 添加弹簧
        layout.addStretch()
        
        # 设置滚动区域内容
        scroll.setWidget(content)
        main_layout.addWidget(scroll)
        
        # 初始状态禁用编辑
        self._set_editing_enabled(False)
    
    def set_map_data(self, map_data: MapData):
        """
        设置地图数据
        
        Args:
            map_data: 地图数据对象
        """
        self.map_data = map_data
        
        # 更新地图信息
        self.map_size_label.setText(f"{map_data.width} x {map_data.height}")
        self.tile_count_label.setText(f"{map_data.get_tile_count()}")
        
        # 统计可行走瓦片
        walkable_count = map_data.get_walkable_count()
        walkable_percent = walkable_count * 100.0 / map_data.get_tile_count() if map_data.get_tile_count() > 0 else 0
        self.walkable_count_label.setText(f"{walkable_count} ({walkable_percent:.1f}%)")
    
    def set_selected_tile(self, x: int, y: int, tile: MapTile):
        """
        设置选中的瓦片
        
        Args:
            x: X 坐标
            y: Y 坐标
            tile: 瓦片对象
        """
        if not tile:
            return
        
        self.current_tile = tile.copy()  # 创建副本用于编辑
        self.current_pos = (x, y)
        
        # 更新瓦片信息
        self.tile_pos_label.setText(f"({x}, {y})")
        self.tile_walkable_label.setText("是" if tile.is_walkable() else "否")
        self.tile_portal_label.setText("是" if tile.has_portal() else "否")
        
        # 更新编辑框（阻止信号）
        self._block_signals(True)
        
        self.bg_spin.setValue(tile.background)
        self.mid_spin.setValue(tile.middle)
        self.obj_spin.setValue(tile.object)
        self.door_index_spin.setValue(tile.door_index)
        self.door_offset_spin.setValue(tile.door_offset)
        self.anim_frame_spin.setValue(tile.anim_frame)
        self.anim_tick_spin.setValue(tile.anim_tick)
        self.area_spin.setValue(tile.area)
        self.light_spin.setValue(tile.light)
        
        self._block_signals(False)
        
        # 启用编辑
        self._set_editing_enabled(True)
        self.apply_btn.setEnabled(False)
        self.reset_btn.setEnabled(False)
    
    def clear_selection(self):
        """清除选择"""
        self.current_tile = None
        self.current_pos = None
        
        self.tile_pos_label.setText("未选择")
        self.tile_walkable_label.setText("-")
        self.tile_portal_label.setText("-")
        
        self._set_editing_enabled(False)
        self.apply_btn.setEnabled(False)
        self.reset_btn.setEnabled(False)
    
    def _set_editing_enabled(self, enabled: bool):
        """设置编辑控件是否启用"""
        self.bg_spin.setEnabled(enabled)
        self.mid_spin.setEnabled(enabled)
        self.obj_spin.setEnabled(enabled)
        self.door_index_spin.setEnabled(enabled)
        self.door_offset_spin.setEnabled(enabled)
        self.anim_frame_spin.setEnabled(enabled)
        self.anim_tick_spin.setEnabled(enabled)
        self.area_spin.setEnabled(enabled)
        self.light_spin.setEnabled(enabled)
    
    def _block_signals(self, block: bool):
        """阻止/恢复所有编辑控件的信号"""
        self.bg_spin.blockSignals(block)
        self.mid_spin.blockSignals(block)
        self.obj_spin.blockSignals(block)
        self.door_index_spin.blockSignals(block)
        self.door_offset_spin.blockSignals(block)
        self.anim_frame_spin.blockSignals(block)
        self.anim_tick_spin.blockSignals(block)
        self.area_spin.blockSignals(block)
        self.light_spin.blockSignals(block)
    
    def _on_property_changed(self):
        """属性值改变"""
        if not self.current_tile:
            return
        
        # 启用应用和重置按钮
        self.apply_btn.setEnabled(True)
        self.reset_btn.setEnabled(True)
    
    def _apply_changes(self):
        """应用修改"""
        if not self.current_tile or not self.current_pos or not self.map_data:
            return
        
        # 从控件读取值
        self.current_tile.background = self.bg_spin.value()
        self.current_tile.middle = self.mid_spin.value()
        self.current_tile.object = self.obj_spin.value()
        self.current_tile.door_index = self.door_index_spin.value()
        self.current_tile.door_offset = self.door_offset_spin.value()
        self.current_tile.anim_frame = self.anim_frame_spin.value()
        self.current_tile.anim_tick = self.anim_tick_spin.value()
        self.current_tile.area = self.area_spin.value()
        self.current_tile.light = self.light_spin.value()
        
        # 更新地图数据
        x, y = self.current_pos
        self.map_data.set_tile(x, y, self.current_tile.copy())
        
        # 更新显示信息
        self.tile_walkable_label.setText("是" if self.current_tile.is_walkable() else "否")
        self.tile_portal_label.setText("是" if self.current_tile.has_portal() else "否")
        
        # 发送修改信号
        self.tile_modified.emit(x, y, self.current_tile)
        
        # 禁用按钮
        self.apply_btn.setEnabled(False)
        self.reset_btn.setEnabled(False)
    
    def _reset_values(self):
        """重置为原始值"""
        if not self.current_tile or not self.current_pos or not self.map_data:
            return
        
        # 从地图数据重新读取
        x, y = self.current_pos
        original_tile = self.map_data.get_tile(x, y)
        
        if original_tile:
            self.current_tile = original_tile.copy()
            
            # 更新控件
            self._block_signals(True)
            
            self.bg_spin.setValue(original_tile.background)
            self.mid_spin.setValue(original_tile.middle)
            self.obj_spin.setValue(original_tile.object)
            self.door_index_spin.setValue(original_tile.door_index)
            self.door_offset_spin.setValue(original_tile.door_offset)
            self.anim_frame_spin.setValue(original_tile.anim_frame)
            self.anim_tick_spin.setValue(original_tile.anim_tick)
            self.area_spin.setValue(original_tile.area)
            self.light_spin.setValue(original_tile.light)
            
            self._block_signals(False)
            
            # 禁用按钮
            self.apply_btn.setEnabled(False)
            self.reset_btn.setEnabled(False)
