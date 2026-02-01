"""
图层控制面板

提供图层可见性控制、网格显示、可行走性显示等选项。
"""

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
    QCheckBox, QSlider, QLabel, QPushButton
)


class LayerPanel(QWidget):
    """
    图层控制面板
    
    控制地图显示选项：
    - 图层可见性（背景、中间、物件）
    - 网格显示
    - 可行走性显示
    - 缩放控制
    """
    
    # 信号
    layer_visibility_changed = pyqtSignal(bool, bool, bool)  # (bg, mid, obj)
    grid_visibility_changed = pyqtSignal(bool)
    walkability_visibility_changed = pyqtSignal(bool)
    zoom_changed = pyqtSignal(float)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.init_ui()
    
    def init_ui(self):
        """初始化用户界面"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        
        # 图层控制组
        layer_group = QGroupBox("图层显示")
        layer_layout = QVBoxLayout(layer_group)
        
        # 背景层复选框
        self.bg_checkbox = QCheckBox("背景层 (Tiles)")
        self.bg_checkbox.setChecked(True)
        self.bg_checkbox.stateChanged.connect(self._on_layer_changed)
        layer_layout.addWidget(self.bg_checkbox)
        
        # 中间层复选框
        self.mid_checkbox = QCheckBox("中间层 (SmTiles)")
        self.mid_checkbox.setChecked(True)
        self.mid_checkbox.stateChanged.connect(self._on_layer_changed)
        layer_layout.addWidget(self.mid_checkbox)
        
        # 物件层复选框
        self.obj_checkbox = QCheckBox("物件层 (Objects)")
        self.obj_checkbox.setChecked(True)
        self.obj_checkbox.stateChanged.connect(self._on_layer_changed)
        layer_layout.addWidget(self.obj_checkbox)
        
        # 全选/全不选按钮
        layer_button_layout = QHBoxLayout()
        
        self.select_all_btn = QPushButton("全选")
        self.select_all_btn.clicked.connect(self._select_all_layers)
        layer_button_layout.addWidget(self.select_all_btn)
        
        self.select_none_btn = QPushButton("全不选")
        self.select_none_btn.clicked.connect(self._select_no_layers)
        layer_button_layout.addWidget(self.select_none_btn)
        
        layer_layout.addLayout(layer_button_layout)
        
        layout.addWidget(layer_group)
        
        # 显示选项组
        display_group = QGroupBox("显示选项")
        display_layout = QVBoxLayout(display_group)
        
        # 网格显示
        self.grid_checkbox = QCheckBox("显示网格")
        self.grid_checkbox.setChecked(False)
        self.grid_checkbox.stateChanged.connect(self._on_grid_changed)
        display_layout.addWidget(self.grid_checkbox)
        
        # 可行走性显示
        self.walkability_checkbox = QCheckBox("显示可行走区域")
        self.walkability_checkbox.setChecked(False)
        self.walkability_checkbox.stateChanged.connect(self._on_walkability_changed)
        display_layout.addWidget(self.walkability_checkbox)
        
        layout.addWidget(display_group)
        
        # 缩放控制组
        zoom_group = QGroupBox("缩放控制")
        zoom_layout = QVBoxLayout(zoom_group)
        
        # 缩放值标签
        zoom_label_layout = QHBoxLayout()
        zoom_label_layout.addWidget(QLabel("缩放:"))
        self.zoom_value_label = QLabel("100%")
        zoom_label_layout.addWidget(self.zoom_value_label)
        zoom_label_layout.addStretch()
        zoom_layout.addLayout(zoom_label_layout)
        
        # 缩放滑块
        self.zoom_slider = QSlider(Qt.Horizontal)
        self.zoom_slider.setMinimum(10)   # 10% = 0.1x
        self.zoom_slider.setMaximum(500)  # 500% = 5.0x
        self.zoom_slider.setValue(100)    # 100% = 1.0x
        self.zoom_slider.setTickPosition(QSlider.TicksBelow)
        self.zoom_slider.setTickInterval(50)
        self.zoom_slider.valueChanged.connect(self._on_zoom_slider_changed)
        zoom_layout.addWidget(self.zoom_slider)
        
        # 缩放按钮
        zoom_button_layout = QHBoxLayout()
        
        self.zoom_in_btn = QPushButton("放大 (+)")
        self.zoom_in_btn.clicked.connect(self._zoom_in)
        zoom_button_layout.addWidget(self.zoom_in_btn)
        
        self.zoom_out_btn = QPushButton("缩小 (-)")
        self.zoom_out_btn.clicked.connect(self._zoom_out)
        zoom_button_layout.addWidget(self.zoom_out_btn)
        
        self.zoom_reset_btn = QPushButton("重置")
        self.zoom_reset_btn.clicked.connect(self._zoom_reset)
        zoom_button_layout.addWidget(self.zoom_reset_btn)
        
        zoom_layout.addLayout(zoom_button_layout)
        
        layout.addWidget(zoom_group)
        
        # 添加弹簧，将内容推到顶部
        layout.addStretch()
    
    def _on_layer_changed(self):
        """图层可见性改变"""
        bg = self.bg_checkbox.isChecked()
        mid = self.mid_checkbox.isChecked()
        obj = self.obj_checkbox.isChecked()
        self.layer_visibility_changed.emit(bg, mid, obj)
    
    def _select_all_layers(self):
        """全选所有图层"""
        self.bg_checkbox.setChecked(True)
        self.mid_checkbox.setChecked(True)
        self.obj_checkbox.setChecked(True)
    
    def _select_no_layers(self):
        """取消选择所有图层"""
        self.bg_checkbox.setChecked(False)
        self.mid_checkbox.setChecked(False)
        self.obj_checkbox.setChecked(False)
    
    def _on_grid_changed(self, state):
        """网格显示改变"""
        self.grid_visibility_changed.emit(state == Qt.Checked)
    
    def _on_walkability_changed(self, state):
        """可行走性显示改变"""
        self.walkability_visibility_changed.emit(state == Qt.Checked)
    
    def _on_zoom_slider_changed(self, value):
        """缩放滑块改变"""
        zoom_factor = value / 100.0
        self.zoom_value_label.setText(f"{value}%")
        self.zoom_changed.emit(zoom_factor)
    
    def _zoom_in(self):
        """放大"""
        current = self.zoom_slider.value()
        new_value = min(self.zoom_slider.maximum(), int(current * 1.2))
        self.zoom_slider.setValue(new_value)
    
    def _zoom_out(self):
        """缩小"""
        current = self.zoom_slider.value()
        new_value = max(self.zoom_slider.minimum(), int(current / 1.2))
        self.zoom_slider.setValue(new_value)
    
    def _zoom_reset(self):
        """重置缩放"""
        self.zoom_slider.setValue(100)
    
    def set_zoom(self, zoom_factor: float):
        """
        设置缩放值（外部调用）
        
        Args:
            zoom_factor: 缩放因子（1.0 = 100%）
        """
        value = int(zoom_factor * 100)
        # 阻止信号，避免循环
        self.zoom_slider.blockSignals(True)
        self.zoom_slider.setValue(value)
        self.zoom_value_label.setText(f"{value}%")
        self.zoom_slider.blockSignals(False)
    
    def set_layer_visibility(self, bg: bool, mid: bool, obj: bool):
        """
        设置图层可见性（外部调用）
        
        Args:
            bg: 背景层是否可见
            mid: 中间层是否可见
            obj: 物件层是否可见
        """
        self.bg_checkbox.blockSignals(True)
        self.mid_checkbox.blockSignals(True)
        self.obj_checkbox.blockSignals(True)
        
        self.bg_checkbox.setChecked(bg)
        self.mid_checkbox.setChecked(mid)
        self.obj_checkbox.setChecked(obj)
        
        self.bg_checkbox.blockSignals(False)
        self.mid_checkbox.blockSignals(False)
        self.obj_checkbox.blockSignals(False)
    
    def set_grid_visible(self, visible: bool):
        """
        设置网格可见性（外部调用）
        
        Args:
            visible: 是否可见
        """
        self.grid_checkbox.blockSignals(True)
        self.grid_checkbox.setChecked(visible)
        self.grid_checkbox.blockSignals(False)
    
    def set_walkability_visible(self, visible: bool):
        """
        设置可行走性可见性（外部调用）
        
        Args:
            visible: 是否可见
        """
        self.walkability_checkbox.blockSignals(True)
        self.walkability_checkbox.setChecked(visible)
        self.walkability_checkbox.blockSignals(False)
    
    def get_layer_visibility(self) -> tuple:
        """
        获取当前图层可见性
        
        Returns:
            (bg, mid, obj) 元组
        """
        return (
            self.bg_checkbox.isChecked(),
            self.mid_checkbox.isChecked(),
            self.obj_checkbox.isChecked()
        )
    
    def is_grid_visible(self) -> bool:
        """获取网格是否可见"""
        return self.grid_checkbox.isChecked()
    
    def is_walkability_visible(self) -> bool:
        """获取可行走性是否可见"""
        return self.walkability_checkbox.isChecked()
    
    def get_zoom(self) -> float:
        """获取当前缩放值"""
        return self.zoom_slider.value() / 100.0
