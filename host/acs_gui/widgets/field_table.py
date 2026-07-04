"""Editable bit-field table (GUI plan §5.2, §7.2-§7.4).

One row per register field: name+bit range | one or more read-only value
columns (EEPROM / Shadow) | an Edit column of per-field editors.
Single-bit fields edit as checkboxes, multi-bit as bounded spinboxes
(plan §6.1). Emits `edited` on any user change; programmatic updates via
set_edits_from_raw() do not re-emit.
"""
from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox, QHeaderView, QSpinBox, QTableWidget, QTableWidgetItem,
)

from ..registers import Register


class FieldTable(QTableWidget):
    edited = Signal()

    def __init__(self, register: Register,
                 value_columns: list[tuple[str, str]],
                 editable: bool = True, parent=None):
        n_rows = len(register.fields)
        n_cols = 1 + len(value_columns) + (1 if editable else 0)
        super().__init__(n_rows, n_cols, parent)
        self._reg = register
        self._updating = False
        self._editors: dict[str, QCheckBox | QSpinBox] = {}
        self._value_items: dict[str, dict[str, QTableWidgetItem]] = {
            key: {} for key, _ in value_columns}

        headers = ["Field"] + [title for _, title in value_columns]
        if editable:
            headers.append("Edit")
        self.setHorizontalHeaderLabels(headers)
        self.verticalHeader().setVisible(False)
        self.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.setSelectionMode(QTableWidget.SelectionMode.NoSelection)

        for row, f in enumerate(register.fields):
            rng = f"[{f.msb}]" if f.width == 1 else f"[{f.msb}:{f.lsb}]"
            name_item = QTableWidgetItem(f"{f.name} {rng}")
            name_item.setFlags(Qt.ItemFlag.ItemIsEnabled)
            if f.description:
                name_item.setToolTip(f.description)
            self.setItem(row, 0, name_item)

            for col, (key, _) in enumerate(value_columns, start=1):
                item = QTableWidgetItem("—")
                item.setFlags(Qt.ItemFlag.ItemIsEnabled)
                item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                self.setItem(row, col, item)
                self._value_items[key][f.name] = item

            if editable:
                edit_col = 1 + len(value_columns)
                if f.width == 1:
                    w: QCheckBox | QSpinBox = QCheckBox()
                    w.toggled.connect(self._on_user_edit)
                else:
                    w = QSpinBox()
                    w.setRange(0, f.max)
                    w.setToolTip(f"0..{f.max} (0x0..0x{f.max:X})")
                    w.valueChanged.connect(self._on_user_edit)
                self.setCellWidget(row, edit_col, w)
                self._editors[f.name] = w

        hdr = self.horizontalHeader()
        hdr.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        for c in range(1, n_cols):
            hdr.setSectionResizeMode(c, QHeaderView.ResizeMode.Stretch)

    # ------------------------------------------------------------------

    def _on_user_edit(self, *_):
        if not self._updating:
            self.edited.emit()

    def set_values(self, column_key: str, raw: int) -> None:
        """Fill a read-only value column from a raw register word."""
        for f in self._reg.fields:
            v = (raw >> f.lsb) & f.max
            text = str(v) if f.width == 1 else f"{v} (0x{v:X})"
            self._value_items[column_key][f.name].setText(text)

    def set_edits_from_raw(self, raw: int) -> None:
        """Load the Edit column from a raw word (no `edited` emission)."""
        self._updating = True
        try:
            for f in self._reg.fields:
                v = (raw >> f.lsb) & f.max
                w = self._editors[f.name]
                if isinstance(w, QCheckBox):
                    w.setChecked(bool(v))
                else:
                    w.setValue(v)
        finally:
            self._updating = False

    def edit_raw(self) -> int:
        """Assemble the raw word currently held by the Edit column."""
        raw = 0
        for f in self._reg.fields:
            w = self._editors[f.name]
            v = int(w.isChecked()) if isinstance(w, QCheckBox) else w.value()
            raw |= v << f.lsb
        return raw

    def set_editor_enabled(self, field_name: str, enabled: bool) -> None:
        self._editors[field_name].setEnabled(enabled)
