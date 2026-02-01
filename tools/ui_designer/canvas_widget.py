import json
import os
from typing import Dict, Optional

from PyQt5.QtCore import QPointF, QRectF, Qt, pyqtSignal
from PyQt5.QtGui import QColor, QPainter, QPen
from PyQt5.QtWidgets import QGraphicsPixmapItem, QGraphicsScene, QGraphicsView

from control_item import ControlRectItem
from wil_loader import WILLoader


class CanvasWidget(QGraphicsView):
    control_selected = pyqtSignal(object)
    controls_changed = pyqtSignal()
    mouse_moved = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.scene = QGraphicsScene(self)
        self.setScene(self.scene)
        self.setRenderHint(QPainter.Antialiasing, True)
        self.setDragMode(QGraphicsView.RubberBandDrag)
        self.setMouseTracking(True)

        self.background_item: Optional[QGraphicsPixmapItem] = None
        self.background_meta: Optional[Dict] = None
        self.controls = []
        self.grid_size = 10
        self.snap_size = 5
        self.show_grid = True
        self.snap_enabled = True
        self.current_screen = "login"

        self.scene.selectionChanged.connect(self._on_selection_changed)

    # ---------------- Scene management ----------------
    def clear_canvas(self, clear_background: bool = True):
        for item in list(self.controls):
            self.scene.removeItem(item)
        self.controls.clear()
        if clear_background and self.background_item:
            self.scene.removeItem(self.background_item)
            self.background_item = None
            self.background_meta = None
        self.scene.update()
        self.controls_changed.emit()
        self.control_selected.emit(None)

    def load_background(self, pixmap, meta: Optional[Dict] = None):
        if not pixmap:
            return
        if self.background_item:
            self.scene.removeItem(self.background_item)
        self.background_item = self.scene.addPixmap(pixmap)
        self.background_item.setZValue(-1)
        self.scene.setSceneRect(QRectF(0, 0, pixmap.width(), pixmap.height()))
        self.background_meta = meta or self.background_meta

    def add_control(self, control_type: str, control_id: Optional[str] = None, rect: Optional[QRectF] = None):
        if rect is None:
            rect = QRectF(0, 0, 120, 36)
        if not control_id:
            control_id = self._next_id(control_type)
        item = ControlRectItem(control_id, control_type, rect)
        item.set_snap(self.snap_size, self.snap_enabled)
        item.changed_callback = self._on_item_changed
        item.selected_callback = self._on_item_selected
        self.scene.addItem(item)
        self.controls.append(item)
        item.setSelected(True)
        self.controls_changed.emit()
        return item

    def delete_selected_controls(self):
        removed = False
        for item in list(self.scene.selectedItems()):
            if isinstance(item, ControlRectItem):
                self.controls.remove(item)
                self.scene.removeItem(item)
                removed = True
        if removed:
            self.controls_changed.emit()
            self.control_selected.emit(self.selected_control())

    def selected_control(self):
        for item in self.scene.selectedItems():
            if isinstance(item, ControlRectItem):
                return item
        return None

    # ---------------- Import/export ----------------
    def export_to_json(self, screen_name: Optional[str] = None) -> Dict:
        if screen_name:
            self.current_screen = screen_name
        rect = self.scene.sceneRect()
        screen = {
            "design_resolution": {"width": int(rect.width()), "height": int(rect.height())},
            "controls": {c.control_id: c.to_dict() for c in self.controls},
        }
        if self.background_meta:
            screen["background"] = self.background_meta
        return {"version": "1.0", "screens": {self.current_screen: screen}}

    def import_from_json(self, data: Dict, base_dir: Optional[str] = None):
        screens = data.get("screens") or {}
        if not screens:
            return
        name, screen = next(iter(screens.items()))
        self.current_screen = name
        self.clear_canvas(clear_background=True)

        bg = screen.get("background")
        if bg:
            wil_path = bg.get("wil_file")
            if base_dir and wil_path and not os.path.isabs(wil_path):
                wil_path = os.path.join(base_dir, wil_path)
            frame = bg.get("frame_index", 0)
            pix = WILLoader.load_wil_frame(wil_path, frame) if wil_path else None
            if pix:
                self.load_background(pix, {"wil_file": bg.get("wil_file"), "frame_index": frame})

        for control_id, cfg in (screen.get("controls") or {}).items():
            bounds = cfg.get("bounds", {})
            rect = QRectF(0, 0, bounds.get("w", 80), bounds.get("h", 30))
            item = self.add_control(cfg.get("type", "clickable_area"), control_id, rect)
            item.setPos(bounds.get("x", 0), bounds.get("y", 0))

        self.controls_changed.emit()

    # ---------------- Utilities ----------------
    def is_id_available(self, new_id: str, control=None) -> bool:
        for item in self.controls:
            if item is control:
                continue
            if item.control_id == new_id:
                return False
        return True

    def rename_control(self, control, new_id: str) -> bool:
        if not control or not new_id:
            return False
        if not self.is_id_available(new_id, control):
            return False
        control.control_id = new_id
        control.setToolTip(new_id)
        self.controls_changed.emit()
        return True

    def refresh_control(self, control):
        if control and control in self.controls:
            self.scene.update()
            self.controls_changed.emit()

    def set_show_grid(self, enabled: bool):
        self.show_grid = enabled
        self.viewport().update()

    def set_snap_to_grid(self, enabled: bool):
        self.snap_enabled = enabled
        for item in self.controls:
            item.set_snap(self.snap_size, enabled)

    def set_snap_value(self, snap: int):
        self.snap_size = snap
        for item in self.controls:
            item.set_snap(snap, self.snap_enabled)

    # ---------------- Event hooks ----------------
    def drawForeground(self, painter: QPainter, rect: QRectF):
        super().drawForeground(painter, rect)
        if not self.show_grid:
            return
        painter.save()
        pen = QPen(QColor(0, 0, 0, 40))
        pen.setCosmetic(True)
        painter.setPen(pen)
        left = int(rect.left()) - (int(rect.left()) % self.grid_size)
        top = int(rect.top()) - (int(rect.top()) % self.grid_size)
        for x in range(left, int(rect.right()) + self.grid_size, self.grid_size):
            painter.drawLine(x, int(rect.top()), x, int(rect.bottom()))
        for y in range(top, int(rect.bottom()) + self.grid_size, self.grid_size):
            painter.drawLine(int(rect.left()), y, int(rect.right()), y)
        painter.restore()

    def mouseMoveEvent(self, event):
        scene_pos = self.mapToScene(event.pos())
        self.mouse_moved.emit(f"{int(scene_pos.x())}, {int(scene_pos.y())}")
        super().mouseMoveEvent(event)

    def _on_selection_changed(self):
        self.control_selected.emit(self.selected_control())

    def _on_item_changed(self, item):
        if self.snap_enabled:
            item.set_snap(self.snap_size, True)
        self.controls_changed.emit()

    def _on_item_selected(self, item):
        self.control_selected.emit(item)

    def _next_id(self, control_type: str) -> str:
        base = control_type
        counter = 1
        existing = {c.control_id for c in self.controls}
        new_id = f"{base}_{counter}"
        while new_id in existing:
            counter += 1
            new_id = f"{base}_{counter}"
        return new_id
