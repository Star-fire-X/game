from typing import Callable, Optional

from PyQt5.QtCore import QPointF, QRectF, Qt
from PyQt5.QtGui import QColor, QCursor, QPen, QBrush
from PyQt5.QtWidgets import QGraphicsItem, QGraphicsRectItem


class ControlRectItem(QGraphicsRectItem):
    HANDLE_SIZE = 6
    MIN_SIZE = 10

    def __init__(self, control_id: str, control_type: str, bounds: QRectF):
        super().__init__(QRectF(0, 0, bounds.width(), bounds.height()))
        self.control_id = control_id
        self.control_type = control_type
        self.setPos(bounds.x(), bounds.y())
        self.base_color = self.color_for_type(control_type)
        self.snap_size = 5
        self.snap_enabled = True
        self.drag_start_scene = QPointF()
        self.orig_rect = QRectF()
        self.orig_pos = QPointF()
        self.resize_dir: Optional[str] = None
        self.changed_callback: Optional[Callable[["ControlRectItem"], None]] = None
        self.selected_callback: Optional[Callable[["ControlRectItem"], None]] = None

        self.setZValue(1)
        self.setFlags(
            QGraphicsItem.ItemIsSelectable
            | QGraphicsItem.ItemIsMovable
            | QGraphicsItem.ItemSendsGeometryChanges
        )
        self.setAcceptHoverEvents(True)
        self.setToolTip(control_id)
        self._update_style()

    def _update_style(self):
        pen = QPen(self.base_color, 2)
        pen.setCosmetic(True)
        fill = QBrush(QColor(self.base_color.red(), self.base_color.green(), self.base_color.blue(), 40))
        self.setPen(pen)
        self.setBrush(fill)

    @staticmethod
    def color_for_type(control_type: str) -> QColor:
        colors = {
            "text_input": QColor("#4a90e2"),
            "button": QColor("#2ecc71"),
            "clickable_area": QColor("#e74c3c"),
            "background_texture": QColor("#bdc3c7"),
        }
        return colors.get(control_type, QColor("#9b59b6"))

    def set_control_type(self, control_type: str):
        self.control_type = control_type
        self.base_color = self.color_for_type(control_type)
        self._update_style()
        self._notify_changed()

    def set_snap(self, snap_size: int, enabled: bool = True):
        self.snap_size = snap_size
        self.snap_enabled = enabled

    def set_bounds(self, x: int, y: int, w: int, h: int):
        w = max(w, self.MIN_SIZE)
        h = max(h, self.MIN_SIZE)
        self.prepareGeometryChange()
        self.setRect(0, 0, w, h)
        self.setPos(x, y)
        self._notify_changed()

    def to_dict(self) -> dict:
        rect = self.rect()
        pos = self.pos()
        return {
            "type": self.control_type,
            "bounds": {
                "x": int(pos.x()),
                "y": int(pos.y()),
                "w": int(rect.width()),
                "h": int(rect.height()),
            },
        }

    # -------------------- Events --------------------
    def hoverMoveEvent(self, event):
        handle = self._hit_handle(event.pos())
        self._set_cursor(handle)
        super().hoverMoveEvent(event)

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.resize_dir = self._hit_handle(event.pos())
            self.drag_start_scene = event.scenePos()
            self.orig_rect = QRectF(self.rect())
            self.orig_pos = QPointF(self.pos())
            if self.selected_callback:
                self.selected_callback(self)
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self.resize_dir:
            delta = event.scenePos() - self.drag_start_scene
            new_x = self.orig_pos.x()
            new_y = self.orig_pos.y()
            new_w = self.orig_rect.width()
            new_h = self.orig_rect.height()

            if "l" in self.resize_dir:
                new_x = self.orig_pos.x() + delta.x()
                new_w = self.orig_rect.width() - delta.x()
            if "r" in self.resize_dir:
                new_w = self.orig_rect.width() + delta.x()
            if "t" in self.resize_dir:
                new_y = self.orig_pos.y() + delta.y()
                new_h = self.orig_rect.height() - delta.y()
            if "b" in self.resize_dir:
                new_h = self.orig_rect.height() + delta.y()

            new_w = max(new_w, self.MIN_SIZE)
            new_h = max(new_h, self.MIN_SIZE)

            if self.snap_enabled and self.snap_size > 0:
                new_x = self._snap_value(new_x, self.snap_size)
                new_y = self._snap_value(new_y, self.snap_size)
                new_w = max(self.MIN_SIZE, self._snap_value(new_w, self.snap_size))
                new_h = max(self.MIN_SIZE, self._snap_value(new_h, self.snap_size))

            self.prepareGeometryChange()
            self.setRect(0, 0, new_w, new_h)
            self.setPos(new_x, new_y)
            self._notify_changed()
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        self.resize_dir = None
        super().mouseReleaseEvent(event)

    def itemChange(self, change, value):
        if change == QGraphicsItem.ItemPositionChange and self.snap_enabled and self.snap_size > 0:
            pos: QPointF = value
            snapped = QPointF(
                self._snap_value(pos.x(), self.snap_size),
                self._snap_value(pos.y(), self.snap_size),
            )
            return snapped
        if change == QGraphicsItem.ItemSelectedHasChanged:
            if self.selected_callback:
                self.selected_callback(self if value else None)
        if change == QGraphicsItem.ItemPositionHasChanged:
            self._notify_changed()
        return super().itemChange(change, value)

    # -------------------- Helpers --------------------
    def _handle_rects(self):
        rect = self.rect()
        s = self.HANDLE_SIZE
        x0, y0 = rect.left(), rect.top()
        x1, y1 = rect.right(), rect.bottom()
        cx = rect.center().x()
        cy = rect.center().y()
        return {
            "tl": QRectF(x0 - s / 2, y0 - s / 2, s, s),
            "t": QRectF(cx - s / 2, y0 - s / 2, s, s),
            "tr": QRectF(x1 - s / 2, y0 - s / 2, s, s),
            "r": QRectF(x1 - s / 2, cy - s / 2, s, s),
            "br": QRectF(x1 - s / 2, y1 - s / 2, s, s),
            "b": QRectF(cx - s / 2, y1 - s / 2, s, s),
            "bl": QRectF(x0 - s / 2, y1 - s / 2, s, s),
            "l": QRectF(x0 - s / 2, cy - s / 2, s, s),
        }

    def _hit_handle(self, pos: QPointF) -> Optional[str]:
        for key, rect in self._handle_rects().items():
            if rect.contains(pos):
                return key
        return None

    def _set_cursor(self, handle: Optional[str]):
        cursors = {
            "tl": Qt.SizeFDiagCursor,
            "br": Qt.SizeFDiagCursor,
            "tr": Qt.SizeBDiagCursor,
            "bl": Qt.SizeBDiagCursor,
            "t": Qt.SizeVerCursor,
            "b": Qt.SizeVerCursor,
            "l": Qt.SizeHorCursor,
            "r": Qt.SizeHorCursor,
        }
        if handle:
            self.setCursor(QCursor(cursors.get(handle, Qt.ArrowCursor)))
        else:
            self.setCursor(Qt.ArrowCursor)

    def _snap_value(self, value: float, step: int) -> float:
        return round(value / step) * step

    def _notify_changed(self):
        if self.changed_callback:
            self.changed_callback(self)
