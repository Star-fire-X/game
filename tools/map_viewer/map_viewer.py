"""
地图查看器主窗口

Legend2 地图查看和编辑工具的主窗口。
"""

import os
import sys
import time
from typing import Optional
from PyQt5.QtCore import Qt, QDir, QThread, pyqtSignal
from PyQt5.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QSplitter,
    QFileDialog, QMessageBox, QStatusBar, QMenuBar, QMenu,
    QAction, QToolBar, QTreeWidget, QTreeWidgetItem, QLabel
)
from PyQt5.QtGui import QIcon, QKeySequence

from tile_data import MapData
from map_loader import MapLoader, list_map_files
from map_canvas import MapCanvas
from layer_panel import LayerPanel
from property_panel import PropertyPanel


class MapViewerWindow(QMainWindow):
    """
    地图查看器主窗口
    
    布局：
    - 左侧：文件浏览树
    - 中央：地图画布
    - 右侧：图层面板 + 属性面板
    - 顶部：菜单栏和工具栏
    - 底部：状态栏
    """
    
    def __init__(self):
        super().__init__()

        self.current_map_path: Optional[str] = None
        self.map_data: Optional[MapData] = None
        self.map_directory: str = ""
        self.data_directory: str = ""
        self.modified: bool = False
        self._load_worker: Optional[MapLoadWorker] = None
        self._loading_map_path: Optional[str] = None

        self.init_ui()
        self.setup_connections()
        
        # 尝试自动检测目录
        self._auto_detect_directories()
    
    def init_ui(self):
        """初始化用户界面"""
        self.setWindowTitle("Legend2 地图查看器")
        self.setGeometry(100, 100, 1400, 800)
        
        # 先创建中央部件和组件
        self._create_central_widget()
        
        # 然后创建菜单栏和工具栏（需要引用组件）
        self._create_menu_bar()
        self._create_tool_bar()
        
        # 创建状态栏
        self._create_status_bar()
    
    def _create_central_widget(self):
        """创建中央部件"""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # 主布局
        main_layout = QHBoxLayout(central_widget)
        main_layout.setContentsMargins(0, 0, 0, 0)
        
        # 左右分割器
        main_splitter = QSplitter(Qt.Horizontal)
        
        # 左侧面板：文件浏览器
        self.file_tree = QTreeWidget()
        self.file_tree.setHeaderLabel("地图文件")
        self.file_tree.setMinimumWidth(200)
        self.file_tree.itemDoubleClicked.connect(self._on_file_tree_double_clicked)
        main_splitter.addWidget(self.file_tree)
        
        # 中央画布
        self.canvas = MapCanvas()
        main_splitter.addWidget(self.canvas)
        
        # 右侧面板
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(0, 0, 0, 0)
        
        # 图层面板
        self.layer_panel = LayerPanel()
        right_layout.addWidget(self.layer_panel)
        
        # 属性面板
        self.property_panel = PropertyPanel()
        right_layout.addWidget(self.property_panel)
        
        right_panel.setMaximumWidth(350)
        main_splitter.addWidget(right_panel)
        
        # 设置分割比例
        main_splitter.setStretchFactor(0, 1)  # 文件树
        main_splitter.setStretchFactor(1, 4)  # 画布
        main_splitter.setStretchFactor(2, 1)  # 右侧面板
        
        main_layout.addWidget(main_splitter)
    
    def _create_status_bar(self):
        """创建状态栏"""
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        
        self.status_label = QLabel("就绪")
        self.status_bar.addWidget(self.status_label)
        
        self.coord_label = QLabel("")
        self.status_bar.addPermanentWidget(self.coord_label)
    
    def _create_menu_bar(self):
        """创建菜单栏"""
        menubar = self.menuBar()
        
        # 文件菜单
        file_menu = menubar.addMenu("文件(&F)")
        
        open_action = QAction("打开地图文件(&O)...", self)
        open_action.setShortcut(QKeySequence.Open)
        open_action.triggered.connect(self.open_map_file)
        file_menu.addAction(open_action)
        
        open_dir_action = QAction("打开地图目录(&D)...", self)
        open_dir_action.setShortcut("Ctrl+Shift+O")
        open_dir_action.triggered.connect(self.open_map_directory)
        file_menu.addAction(open_dir_action)
        
        file_menu.addSeparator()
        
        save_action = QAction("保存(&S)", self)
        save_action.setShortcut(QKeySequence.Save)
        save_action.triggered.connect(self.save_map)
        file_menu.addAction(save_action)
        
        save_as_action = QAction("另存为(&A)...", self)
        save_as_action.setShortcut(QKeySequence.SaveAs)
        save_as_action.triggered.connect(self.save_map_as)
        file_menu.addAction(save_as_action)
        
        file_menu.addSeparator()
        
        exit_action = QAction("退出(&X)", self)
        exit_action.setShortcut(QKeySequence.Quit)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)
        
        # 查看菜单
        view_menu = menubar.addMenu("查看(&V)")
        
        zoom_in_action = QAction("放大(&I)", self)
        zoom_in_action.setShortcut(QKeySequence.ZoomIn)
        zoom_in_action.triggered.connect(self.canvas.zoom_in)
        view_menu.addAction(zoom_in_action)
        
        zoom_out_action = QAction("缩小(&O)", self)
        zoom_out_action.setShortcut(QKeySequence.ZoomOut)
        zoom_out_action.triggered.connect(self.canvas.zoom_out)
        view_menu.addAction(zoom_out_action)
        
        reset_zoom_action = QAction("重置缩放(&R)", self)
        reset_zoom_action.setShortcut("Ctrl+0")
        reset_zoom_action.triggered.connect(self.canvas.reset_zoom)
        view_menu.addAction(reset_zoom_action)
        
        # 设置菜单
        settings_menu = menubar.addMenu("设置(&S)")
        
        set_data_dir_action = QAction("设置 Data 目录(&D)...", self)
        set_data_dir_action.triggered.connect(self.set_data_directory)
        settings_menu.addAction(set_data_dir_action)
        
        # 帮助菜单
        help_menu = menubar.addMenu("帮助(&H)")
        
        about_action = QAction("关于(&A)...", self)
        about_action.triggered.connect(self.show_about)
        help_menu.addAction(about_action)
    
    def _create_tool_bar(self):
        """创建工具栏"""
        toolbar = QToolBar("主工具栏")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)
        
        # 打开文件
        open_action = QAction("打开文件", self)
        open_action.triggered.connect(self.open_map_file)
        toolbar.addAction(open_action)
        
        # 打开目录
        open_dir_action = QAction("打开目录", self)
        open_dir_action.triggered.connect(self.open_map_directory)
        toolbar.addAction(open_dir_action)
        
        toolbar.addSeparator()
        
        # 保存
        save_action = QAction("保存", self)
        save_action.triggered.connect(self.save_map)
        toolbar.addAction(save_action)
        
        toolbar.addSeparator()
        
        # 缩放
        zoom_in_action = QAction("放大", self)
        zoom_in_action.triggered.connect(self.canvas.zoom_in)
        toolbar.addAction(zoom_in_action)
        
        zoom_out_action = QAction("缩小", self)
        zoom_out_action.triggered.connect(self.canvas.zoom_out)
        toolbar.addAction(zoom_out_action)
        
        reset_zoom_action = QAction("重置", self)
        reset_zoom_action.triggered.connect(self.canvas.reset_zoom)
        toolbar.addAction(reset_zoom_action)
    
    def setup_connections(self):
        """设置信号连接"""
        # 图层面板信号
        self.layer_panel.layer_visibility_changed.connect(self.canvas.set_layer_visibility)
        self.layer_panel.grid_visibility_changed.connect(self.canvas.set_grid_visible)
        self.layer_panel.walkability_visibility_changed.connect(self.canvas.set_walkability_visible)
        self.layer_panel.zoom_changed.connect(self.canvas.set_zoom)
        
        # 画布信号
        self.canvas.tile_selected.connect(self._on_tile_selected)
        self.canvas.tile_hovered.connect(self._on_tile_hovered)
        self.canvas.zoom_changed.connect(self.layer_panel.set_zoom)
        
        # 属性面板信号
        self.property_panel.tile_modified.connect(self._on_tile_modified)
    
    def _auto_detect_directories(self):
        """自动检测地图和数据目录"""
        # 尝试查找 Map 和 Data 目录
        search_paths = [
            ".",
            "..",
            "../..",
        ]
        
        for base in search_paths:
            map_path = os.path.join(base, "Map")
            data_path = os.path.join(base, "Data")
            
            if os.path.isdir(map_path):
                self.map_directory = os.path.abspath(map_path)
                self._load_file_tree(self.map_directory)
                self.status_label.setText(f"已加载地图目录: {self.map_directory}")
            
            if os.path.isdir(data_path):
                self.data_directory = os.path.abspath(data_path)
                self.canvas.resource_manager.set_data_directory(self.data_directory)
                self.status_label.setText(f"已设置资源目录: {self.data_directory}")
            
            if self.map_directory and self.data_directory:
                break
    
    def _load_file_tree(self, directory: str):
        """加载文件树"""
        self.file_tree.clear()
        
        map_files = list_map_files(directory)
        
        for map_file in map_files:
            rel_path = os.path.relpath(map_file, directory)
            item = QTreeWidgetItem([rel_path])
            item.setData(0, Qt.UserRole, map_file)
            self.file_tree.addTopLevelItem(item)
        
        self.status_label.setText(f"找到 {len(map_files)} 个地图文件")
    
    def _on_file_tree_double_clicked(self, item: QTreeWidgetItem, column: int):
        """文件树双击事件"""
        map_path = item.data(0, Qt.UserRole)
        if map_path:
            self._load_map(map_path)
    
    def _load_map(self, map_path: str):
        """加载地图"""
        if self._load_worker and self._load_worker.isRunning():
            QMessageBox.information(self, "提示", "正在加载地图，请稍候再试。")
            return

        self.status_label.setText(f"正在加载: {map_path}...")
        self._loading_map_path = map_path

        self._load_worker = MapLoadWorker(map_path)
        self._load_worker.loaded.connect(self._on_map_loaded)
        self._load_worker.failed.connect(self._on_map_load_failed)
        self._load_worker.start()

    def _on_map_loaded(self, map_data: MapData, elapsed: float):
        map_path = self._loading_map_path or self.current_map_path or ""
        self._loading_map_path = None

        self.current_map_path = map_path
        self.map_data = map_data
        self.modified = False

        # 加载到画布
        self.canvas.load_map(map_data, self.data_directory)

        # 更新属性面板
        self.property_panel.set_map_data(map_data)

        # 更新窗口标题
        filename = os.path.basename(map_path) if map_path else "未命名"
        self.setWindowTitle(f"Legend2 地图查看器 - {filename}")

        if PROFILE_LOAD:
            self.status_label.setText(
                f"已加载: {filename} ({map_data.width}x{map_data.height}) "
                f"耗时 {elapsed:.2f}s"
            )
        else:
            self.status_label.setText(f"已加载: {filename} ({map_data.width}x{map_data.height})")

        if self._load_worker:
            self._load_worker.deleteLater()
            self._load_worker = None

    def _on_map_load_failed(self, message: str):
        map_path = self._loading_map_path or ""
        self._loading_map_path = None
        self.status_label.setText("加载失败")
        QMessageBox.critical(self, "错误", f"无法加载地图文件:\n{map_path}\n{message}")

        if self._load_worker:
            self._load_worker.deleteLater()
            self._load_worker = None
    
    def _on_tile_selected(self, x: int, y: int, tile):
        """瓦片被选中"""
        if tile:
            self.property_panel.set_selected_tile(x, y, tile)
    
    def _on_tile_hovered(self, x: int, y: int):
        """鼠标悬停在瓦片上"""
        self.coord_label.setText(f"坐标: ({x}, {y})")
    
    def _on_tile_modified(self, x: int, y: int, tile):
        """瓦片被修改"""
        self.modified = True
        
        # 更新窗口标题
        if self.current_map_path:
            filename = os.path.basename(self.current_map_path)
            self.setWindowTitle(f"Legend2 地图查看器 - {filename} *")
        
        # 刷新画布显示
        self.canvas.invalidate_tile(x, y)
        self.canvas.scene.update()
        
        self.status_label.setText(f"瓦片 ({x}, {y}) 已修改")
    
    def open_map_file(self):
        """打开地图文件"""
        start_dir = self.map_directory if self.map_directory else "."
        
        file_path, _ = QFileDialog.getOpenFileName(
            self, "打开地图文件", start_dir, "地图文件 (*.map *.MAP)"
        )
        
        if file_path:
            self._load_map(file_path)
    
    def open_map_directory(self):
        """打开地图目录"""
        directory = QFileDialog.getExistingDirectory(
            self, "选择地图目录", self.map_directory if self.map_directory else "."
        )
        
        if directory:
            self.map_directory = directory
            self._load_file_tree(directory)
    
    def save_map(self):
        """保存地图"""
        if not self.current_map_path or not self.map_data:
            self.save_map_as()
            return
        
        if MapLoader.save(self.map_data, self.current_map_path, backup=True):
            self.modified = False
            filename = os.path.basename(self.current_map_path)
            self.setWindowTitle(f"Legend2 地图查看器 - {filename}")
            self.status_label.setText(f"已保存: {filename}")
            QMessageBox.information(self, "成功", "地图已保存!")
        else:
            QMessageBox.critical(self, "错误", "保存地图失败!")
    
    def save_map_as(self):
        """另存为"""
        if not self.map_data:
            return
        
        start_dir = self.map_directory if self.map_directory else "."
        
        file_path, _ = QFileDialog.getSaveFileName(
            self, "另存为", start_dir, "地图文件 (*.map)"
        )
        
        if file_path:
            if MapLoader.save(self.map_data, file_path, backup=False):
                self.current_map_path = file_path
                self.modified = False
                filename = os.path.basename(file_path)
                self.setWindowTitle(f"Legend2 地图查看器 - {filename}")
                self.status_label.setText(f"已保存: {filename}")
                QMessageBox.information(self, "成功", "地图已保存!")
            else:
                QMessageBox.critical(self, "错误", "保存地图失败!")
    
    def set_data_directory(self):
        """设置 Data 目录"""
        directory = QFileDialog.getExistingDirectory(
            self, "选择 Data 目录", self.data_directory if self.data_directory else "."
        )
        
        if directory:
            self.data_directory = directory
            self.canvas.resource_manager.set_data_directory(directory)
            self.status_label.setText(f"已设置资源目录: {directory}")
    
    def show_about(self):
        """显示关于对话框"""
        QMessageBox.about(
            self,
            "关于",
            "<h3>Legend2 地图查看器</h3>"
            "<p>用于查看和编辑传奇2游戏地图文件的工具。</p>"
            "<p><b>功能：</b></p>"
            "<ul>"
            "<li>查看地图三层图像（背景、中间、物件）</li>"
            "<li>编辑瓦片属性</li>"
            "<li>显示网格和可行走区域</li>"
            "<li>缩放和平移视图</li>"
            "</ul>"
        )
    
    def closeEvent(self, event):
        """窗口关闭事件"""
        if self.modified:
            reply = QMessageBox.question(
                self,
                "未保存的修改",
                "地图有未保存的修改，是否保存？",
                QMessageBox.Save | QMessageBox.Discard | QMessageBox.Cancel
            )
            
            if reply == QMessageBox.Save:
                self.save_map()
                event.accept()
            elif reply == QMessageBox.Discard:
                event.accept()
            else:
                event.ignore()
        else:
            event.accept()
PROFILE_LOAD = os.environ.get("MAP_VIEWER_PROFILE", "0") == "1"


class MapLoadWorker(QThread):
    loaded = pyqtSignal(object, float)
    failed = pyqtSignal(str)

    def __init__(self, map_path: str):
        super().__init__()
        self.map_path = map_path

    def run(self):
        start = time.perf_counter()
        try:
            map_data = MapLoader.load(self.map_path)
        except Exception as exc:  # pragma: no cover - safety net
            self.failed.emit(str(exc))
            return
        elapsed = time.perf_counter() - start
        if map_data is None:
            self.failed.emit("无法加载地图文件")
            return
        self.loaded.emit(map_data, elapsed)
