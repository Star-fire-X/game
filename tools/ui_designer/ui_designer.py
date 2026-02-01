import json
import os

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QKeySequence
from PyQt5.QtWidgets import (
    QAction,
    QFileDialog,
    QInputDialog,
    QMainWindow,
    QMessageBox,
    QShortcut,
    QSplitter,
    QToolBar,
)

from canvas_widget import CanvasWidget
from property_panel import PropertyPanel
from wil_loader import WILLoader


class UIDesignerWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Legend2 UI Designer")
        self.resize(1200, 800)

        self.canvas = CanvasWidget()
        self.property_panel = PropertyPanel()
        self.property_panel.set_handlers(self.canvas.rename_control, self._on_property_changed)

        self.canvas.control_selected.connect(self.property_panel.load_control)
        self.canvas.controls_changed.connect(self._on_controls_changed)
        self.canvas.mouse_moved.connect(self._on_mouse_moved)

        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(self.canvas)
        splitter.addWidget(self.property_panel)
        splitter.setStretchFactor(0, 3)
        splitter.setStretchFactor(1, 1)
        self.setCentralWidget(splitter)

        self.current_file: str = ""
        self._build_menu()
        self._build_toolbar()
        self._build_shortcuts()
        self.statusBar().showMessage("Ready")

    # ---------------- UI wiring ----------------
    def _build_menu(self):
        menubar = self.menuBar()
        file_menu = menubar.addMenu("File")
        edit_menu = menubar.addMenu("Edit")
        view_menu = menubar.addMenu("View")

        new_action = QAction("New", self)
        new_action.setShortcut(QKeySequence.New)
        new_action.triggered.connect(self.new_layout)
        file_menu.addAction(new_action)

        open_action = QAction("Open JSON...", self)
        open_action.setShortcut(QKeySequence.Open)
        open_action.triggered.connect(self.load_json_layout)
        file_menu.addAction(open_action)

        save_action = QAction("Save", self)
        save_action.setShortcut(QKeySequence.Save)
        save_action.triggered.connect(self.save_json_layout)
        file_menu.addAction(save_action)

        bg_action = QAction("Load WIL Background...", self)
        bg_action.triggered.connect(self.load_wil_background)
        file_menu.addAction(bg_action)

        exit_action = QAction("Exit", self)
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        add_action = QAction("Add Control", self)
        add_action.triggered.connect(lambda: self.add_control("clickable_area"))
        edit_menu.addAction(add_action)

        delete_action = QAction("Delete", self)
        delete_action.setShortcut(QKeySequence.Delete)
        delete_action.triggered.connect(self.delete_selected_control)
        edit_menu.addAction(delete_action)

        self.grid_action = QAction("Show Grid", self, checkable=True, checked=True)
        self.grid_action.triggered.connect(self.toggle_grid)
        view_menu.addAction(self.grid_action)

        self.snap_action = QAction("Snap to Grid", self, checkable=True, checked=True)
        self.snap_action.triggered.connect(self.toggle_snap)
        view_menu.addAction(self.snap_action)

    def _build_toolbar(self):
        toolbar = QToolBar("Tools", self)
        self.addToolBar(toolbar)

        btn_text = QAction("Add Text Input", self)
        btn_text.triggered.connect(lambda: self.add_control("text_input"))
        toolbar.addAction(btn_text)

        btn_button = QAction("Add Button", self)
        btn_button.triggered.connect(lambda: self.add_control("button"))
        toolbar.addAction(btn_button)

        btn_click = QAction("Add Clickable Area", self)
        btn_click.triggered.connect(lambda: self.add_control("clickable_area"))
        toolbar.addAction(btn_click)

    def _build_shortcuts(self):
        QShortcut(QKeySequence.Save, self, activated=self.save_json_layout)
        QShortcut(QKeySequence.New, self, activated=self.new_layout)
        QShortcut(QKeySequence.Delete, self, activated=self.delete_selected_control)

    # ---------------- Actions ----------------
    def new_layout(self):
        self.canvas.clear_canvas(clear_background=True)
        self.property_panel.clear()
        self.current_file = ""
        self.statusBar().showMessage("New layout")

    def add_control(self, control_type: str):
        item = self.canvas.add_control(control_type)
        self.property_panel.load_control(item)

    def delete_selected_control(self):
        self.canvas.delete_selected_controls()
        self.property_panel.load_control(self.canvas.selected_control())

    def load_wil_background(self):
        path, _ = QFileDialog.getOpenFileName(self, "Select WIL File", "", "WIL Files (*.wil *.WIL)")
        if not path:
            return
        frame, ok = QInputDialog.getInt(self, "Frame Index", "Frame index (0-based):", 0, 0, 50000, 1)
        if not ok:
            return
        pix = WILLoader.load_wil_frame(path, frame)
        if not pix:
            QMessageBox.warning(self, "Load Failed", "Failed to load WIL frame.")
            return
        meta = {"wil_file": os.path.basename(path), "frame_index": frame}
        self.canvas.load_background(pix, meta)
        self.statusBar().showMessage(f"Background: {os.path.basename(path)} [{frame}]")

    def load_json_layout(self):
        path, _ = QFileDialog.getOpenFileName(self, "Open ui_layout.json", "", "JSON Files (*.json)")
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
            self.canvas.import_from_json(data, base_dir=os.path.dirname(path))
            self.current_file = path
            self.statusBar().showMessage(f"Loaded {os.path.basename(path)}")
        except Exception as exc:
            QMessageBox.critical(self, "Load Failed", str(exc))

    def save_json_layout(self):
        data = self.canvas.export_to_json()
        path = self.current_file
        if not path:
            path, _ = QFileDialog.getSaveFileName(
                self, "Save ui_layout.json", "ui_layout.json", "JSON Files (*.json)"
            )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(data, fh, indent=2)
            self.current_file = path
            self.statusBar().showMessage(f"Saved {os.path.basename(path)}")
        except Exception as exc:
            QMessageBox.critical(self, "Save Failed", str(exc))

    def toggle_grid(self):
        self.canvas.set_show_grid(self.grid_action.isChecked())

    def toggle_snap(self):
        self.canvas.set_snap_to_grid(self.snap_action.isChecked())

    def _on_property_changed(self, control):
        self.canvas.refresh_control(control)

    def _on_controls_changed(self):
        pass  # placeholder for future status updates

    def _on_mouse_moved(self, text: str):
        self.statusBar().showMessage(f"Cursor: {text}")
