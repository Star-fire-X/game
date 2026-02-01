from typing import Callable, Optional

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QComboBox,
    QFormLayout,
    QLineEdit,
    QMessageBox,
    QSpinBox,
    QWidget,
)


class PropertyPanel(QWidget):
    """Right-side property editor."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._updating = False
        self.current_control = None
        self.rename_handler: Optional[Callable] = None
        self.change_handler: Optional[Callable] = None
        self._build_ui()

    def _build_ui(self):
        self.id_edit = QLineEdit()
        self.type_combo = QComboBox()
        self.type_combo.addItems(["text_input", "button", "clickable_area", "background_texture"])

        self.x_spin = QSpinBox()
        self.y_spin = QSpinBox()
        self.w_spin = QSpinBox()
        self.h_spin = QSpinBox()
        for spin in (self.x_spin, self.y_spin):
            spin.setRange(-9999, 9999)
        for spin in (self.w_spin, self.h_spin):
            spin.setRange(1, 9999)

        form = QFormLayout(self)
        form.addRow("Control ID", self.id_edit)
        form.addRow("Type", self.type_combo)
        form.addRow("X", self.x_spin)
        form.addRow("Y", self.y_spin)
        form.addRow("Width", self.w_spin)
        form.addRow("Height", self.h_spin)
        self.setLayout(form)

        self.id_edit.editingFinished.connect(self._on_id_changed)
        self.type_combo.currentTextChanged.connect(self._on_type_changed)
        for spin in (self.x_spin, self.y_spin, self.w_spin, self.h_spin):
            spin.valueChanged.connect(self._on_geometry_changed)

        self.setEnabled(False)

    def set_handlers(self, rename_handler: Callable, change_handler: Callable):
        self.rename_handler = rename_handler
        self.change_handler = change_handler

    def load_control(self, control):
        self.current_control = control
        self._updating = True
        if control:
            rect = control.rect()
            pos = control.pos()
            self.id_edit.setText(control.control_id)
            self.type_combo.setCurrentText(control.control_type)
            self.x_spin.setValue(int(pos.x()))
            self.y_spin.setValue(int(pos.y()))
            self.w_spin.setValue(int(rect.width()))
            self.h_spin.setValue(int(rect.height()))
            self.setEnabled(True)
        else:
            self.id_edit.clear()
            self.setEnabled(False)
        self._updating = False

    def clear(self):
        self.load_control(None)

    def _on_id_changed(self):
        if self._updating or not self.current_control:
            return
        new_id = self.id_edit.text().strip()
        if not new_id:
            self.id_edit.setText(self.current_control.control_id)
            return
        if self.rename_handler:
            ok = self.rename_handler(self.current_control, new_id)
            if not ok:
                QMessageBox.warning(self, "Duplicate ID", f"ID '{new_id}' already exists.")
                self.id_edit.setText(self.current_control.control_id)
                return
        self._notify_changed()

    def _on_type_changed(self, new_type: str):
        if self._updating or not self.current_control:
            return
        self.current_control.set_control_type(new_type)
        self._notify_changed()

    def _on_geometry_changed(self, _):
        if self._updating or not self.current_control:
            return
        x = self.x_spin.value()
        y = self.y_spin.value()
        w = self.w_spin.value()
        h = self.h_spin.value()
        self.current_control.set_bounds(x, y, w, h)
        self._notify_changed()

    def _notify_changed(self):
        if self.change_handler and self.current_control:
            self.change_handler(self.current_control)
