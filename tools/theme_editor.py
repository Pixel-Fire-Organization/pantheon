#!/usr/bin/env python3
"""Visual editor for game/config/theme.json.

A theme's colours, metrics and fonts are either its own or inherited from the
default theme (see tools/theme.py); this editor makes that distinction visible
and editable per field, instead of hand-editing JSON. It never writes a file
that tools/theme.py itself would refuse: every save runs through
`theme.validate_declaration` first, and a rejected save changes nothing on
disk.

    python3 tools/theme_editor.py
    python3 tools/theme_editor.py --theme-path path/to/theme.json

Stdlib only (tkinter), so there is nothing to install -- except on Windows
under devkitPro's bundled MSYS2 Python, whose `python3` has no Tk support and
no package to add it. Use the Python Launcher instead, which is already on
PATH: `py -3 tools/theme_editor.py`.
"""

import argparse
import copy
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import theme as theme_tool
from ps2lib import theme as themelib

METRIC_FIELDS = list(themelib.INT_METRICS) + ["repeatDelaySeconds", "repeatIntervalSeconds"]


# ---------------------------------------------------------------------------
# Pure logic -- no tkinter, so this half is testable without a display.
# ---------------------------------------------------------------------------

def find_default(decl):
    """The one theme marked default, or None when the declaration has none or
    more than one (an editor must tolerate a declaration mid-edit that a
    stricter reader would refuse)."""
    defaults = [e for e in (decl.get("themes") or []) if e.get("default")]
    return defaults[0] if len(defaults) == 1 else None


def metric_is_float(name):
    return name in ("repeatDelaySeconds", "repeatIntervalSeconds")


def metric_range(name):
    return themelib.REPEAT_RANGE if metric_is_float(name) else themelib.METRIC_RANGES[name]


def field_value(decl, entry, category, key):
    """(value, inherited) for one field of one theme.

    `value` is None when neither the theme nor the default theme provides it
    (an incomplete declaration, mid-edit). `inherited` is always False for the
    default theme itself, since it has nothing to inherit from.
    """
    own = entry.get(category) or {}
    if key in own:
        return own[key], False
    default_entry = find_default(decl)
    if default_entry is None or default_entry is entry:
        return None, False
    resolver = getattr(theme_tool, "resolved_" + category)
    resolved = resolver(default_entry, entry)
    return resolved.get(key), (key in resolved)


def set_own_field(entry, category, key, value):
    entry.setdefault(category, {})[key] = value


def clear_own_field(entry, category, key):
    bucket = entry.get(category)
    if bucket and key in bucket:
        del bucket[key]


def preview_style(decl, entry):
    """The theme's fully resolved colours/metrics/fonts, for the live
    preview. Missing fields are simply absent from the result, so a
    half-finished theme previews with whatever it already has."""
    default_entry = find_default(decl) or entry
    return {
        "colors": theme_tool.resolved_colors(default_entry, entry),
        "metrics": theme_tool.resolved_metrics(default_entry, entry),
        "fonts": theme_tool.resolved_fonts(default_entry, entry),
    }


def new_theme_entry(name):
    return {"name": name, "description": "", "colors": {}, "metrics": {}, "fonts": {}}


def rgba_to_hex(rgba):
    if not rgba or len(rgba) < 3:
        return "#000000"
    r, g, b = (max(0, min(255, int(c))) for c in rgba[:3])
    return "#{:02x}{:02x}{:02x}".format(r, g, b)


def hex_to_rgb(hex_color):
    hex_color = hex_color.lstrip("#")
    return [int(hex_color[i:i + 2], 16) for i in (0, 2, 4)]


# ---------------------------------------------------------------------------
# The editor window
# ---------------------------------------------------------------------------

def _run_app(path):
    import tkinter as tk
    from tkinter import colorchooser, messagebox, simpledialog, ttk

    class ColorRow:
        """One colour role's inherit toggle, swatch and alpha field."""

        def __init__(self, parent, app, role, row):
            self.app = app
            self.role = role
            self.inherit_var = tk.BooleanVar()
            self.alpha_var = tk.StringVar()
            self.rgb = [0, 0, 0]

            self.inherit_check = ttk.Checkbutton(parent, variable=self.inherit_var, command=self._on_inherit_toggle)
            self.inherit_check.grid(row=row, column=0, padx=(4, 2))
            ttk.Label(parent, text=role, width=16).grid(row=row, column=1, sticky="w")
            self.swatch = tk.Button(parent, width=6, command=self._pick_color, relief="ridge")
            self.swatch.grid(row=row, column=2, padx=4)
            ttk.Label(parent, text="alpha").grid(row=row, column=3, sticky="e")
            self.alpha_entry = ttk.Spinbox(parent, from_=0, to=255, width=5, textvariable=self.alpha_var, command=self._on_alpha_changed)
            self.alpha_entry.grid(row=row, column=4, padx=(2, 4))
            self.alpha_entry.bind("<FocusOut>", lambda _e: self._on_alpha_changed())
            self.inherited_label = ttk.Label(parent, text="", foreground="#888888")
            self.inherited_label.grid(row=row, column=5, sticky="w")

        def refresh(self, decl, entry, is_default):
            value, inherited = field_value(decl, entry, "colors", self.role)
            self.inherit_var.set(inherited and not is_default)
            value = value or [0, 0, 0, 255]
            self.rgb = list(value[:3])
            self.alpha_var.set(str(value[3] if len(value) > 3 else 255))
            self.swatch.configure(background=rgba_to_hex(value))
            self.inherited_label.configure(text="(inherited)" if inherited else "")
            self.inherit_check.configure(state="normal" if not is_default else "disabled")
            self._set_value_enabled((not is_default) and not self.inherit_var.get())

        def _set_value_enabled(self, enabled):
            self.alpha_entry.configure(state="normal" if enabled else "disabled")
            self.swatch.configure(state="normal" if enabled else "disabled")

        def _current_value(self):
            try:
                a = int(self.alpha_var.get())
            except ValueError:
                a = 255
            a = max(0, min(255, a))
            return [self.rgb[0], self.rgb[1], self.rgb[2], a]

        def _pick_color(self):
            initial = rgba_to_hex(self._current_value())
            picked = colorchooser.askcolor(color=initial, title="{} colour".format(self.role))
            if picked[0] is None:
                return
            self.rgb = [int(c) for c in picked[0]]
            self.swatch.configure(background=rgba_to_hex(self._current_value()))
            self.inherit_var.set(False)
            self.app.write_field("colors", self.role, self._current_value())

        def _on_alpha_changed(self):
            self.swatch.configure(background=rgba_to_hex(self._current_value()))
            self.inherit_var.set(False)
            self.app.write_field("colors", self.role, self._current_value())

        def _on_inherit_toggle(self):
            if self.inherit_var.get():
                self.app.clear_field("colors", self.role)
            else:
                self.app.write_field("colors", self.role, self._current_value())
                self._set_value_enabled(True)

    class MetricRow:
        def __init__(self, parent, app, name, row):
            self.app = app
            self.name = name
            self.inherit_var = tk.BooleanVar()
            self.value_var = tk.StringVar()

            self.inherit_check = ttk.Checkbutton(parent, variable=self.inherit_var, command=self._on_inherit_toggle)
            self.inherit_check.grid(row=row, column=0, padx=(4, 2))
            ttk.Label(parent, text=name, width=22).grid(row=row, column=1, sticky="w")
            lo, hi = metric_range(name)
            self.entry = ttk.Spinbox(
                parent, from_=lo, to=hi,
                increment=(0.01 if metric_is_float(name) else 1),
                textvariable=self.value_var, width=10, command=self._on_value_changed,
            )
            self.entry.grid(row=row, column=2, padx=4)
            self.entry.bind("<FocusOut>", lambda _e: self._on_value_changed())
            self.range_label = ttk.Label(parent, text="{}..{}".format(lo, hi), foreground="#888888")
            self.range_label.grid(row=row, column=3, sticky="w")
            self.inherited_label = ttk.Label(parent, text="", foreground="#888888")
            self.inherited_label.grid(row=row, column=4, sticky="w")

        def refresh(self, decl, entry, is_default):
            value, inherited = field_value(decl, entry, "metrics", self.name)
            self.inherit_var.set(inherited and not is_default)
            self.value_var.set("" if value is None else str(value))
            self.inherited_label.configure(text="(inherited)" if inherited else "")
            self.inherit_check.configure(state="normal" if not is_default else "disabled")
            self._set_value_enabled((not is_default) and not self.inherit_var.get())

        def _set_value_enabled(self, enabled):
            self.entry.configure(state="normal" if enabled else "disabled")

        def _parsed_value(self):
            raw = self.value_var.get()
            try:
                return float(raw) if metric_is_float(self.name) else int(float(raw))
            except ValueError:
                return None

        def _on_value_changed(self):
            value = self._parsed_value()
            if value is None:
                return
            self.inherit_var.set(False)
            self.app.write_field("metrics", self.name, value)

        def _on_inherit_toggle(self):
            if self.inherit_var.get():
                self.app.clear_field("metrics", self.name)
            else:
                value = self._parsed_value()
                if value is not None:
                    self.app.write_field("metrics", self.name, value)
                self._set_value_enabled(True)

    class FontRow:
        def __init__(self, parent, app, role, row):
            self.app = app
            self.role = role
            self.inherit_var = tk.BooleanVar()
            self.value_var = tk.StringVar()

            self.inherit_check = ttk.Checkbutton(parent, variable=self.inherit_var, command=self._on_inherit_toggle)
            self.inherit_check.grid(row=row, column=0, padx=(4, 2))
            ttk.Label(parent, text=role, width=16).grid(row=row, column=1, sticky="w")
            self.entry = ttk.Entry(parent, textvariable=self.value_var, width=24)
            self.entry.grid(row=row, column=2, padx=4, sticky="w")
            self.entry.bind("<FocusOut>", lambda _e: self._on_value_changed())
            self.entry.bind("<Return>", lambda _e: self._on_value_changed())
            self.inherited_label = ttk.Label(parent, text="", foreground="#888888")
            self.inherited_label.grid(row=row, column=3, sticky="w")

        def refresh(self, decl, entry, is_default):
            value, inherited = field_value(decl, entry, "fonts", self.role)
            self.inherit_var.set(inherited and not is_default)
            self.value_var.set(value or "")
            self.inherited_label.configure(text="(inherited)" if inherited else "")
            self.inherit_check.configure(state="normal" if not is_default else "disabled")
            self._set_value_enabled((not is_default) and not self.inherit_var.get())

        def _set_value_enabled(self, enabled):
            self.entry.configure(state="normal" if enabled else "disabled")

        def _on_value_changed(self):
            self.inherit_var.set(False)
            self.app.write_field("fonts", self.role, self.value_var.get())

        def _on_inherit_toggle(self):
            if self.inherit_var.get():
                self.app.clear_field("fonts", self.role)
            else:
                self.app.write_field("fonts", self.role, self.value_var.get())
                self._set_value_enabled(True)

    class ScrollableFrame(ttk.Frame):
        """The one piece of scaffolding tkinter has no built-in for: a frame
        taller than its viewport, scrolled with a normal scrollbar."""

        def __init__(self, parent):
            super().__init__(parent)
            canvas = tk.Canvas(self, highlightthickness=0)
            scrollbar = ttk.Scrollbar(self, orient="vertical", command=canvas.yview)
            self.body = ttk.Frame(canvas)
            self.body.bind("<Configure>", lambda _e: canvas.configure(scrollregion=canvas.bbox("all")))
            canvas.create_window((0, 0), window=self.body, anchor="nw")
            canvas.configure(yscrollcommand=scrollbar.set)
            canvas.pack(side="left", fill="both", expand=True)
            scrollbar.pack(side="right", fill="y")

    class ThemeEditorApp:
        def __init__(self, root, path):
            self.root = root
            self.path = path
            self.color_roles = theme_tool.engine_color_roles()
            self.font_roles = theme_tool.engine_font_roles()
            self.decl = self._load(path)
            self.theme_index = 0

            root.title("Theme Editor -- {}".format(os.path.basename(path)))
            root.geometry("1260x700")

            self._build_layout()
            self.refresh_theme_list()
            self.select_theme(0)

        # --- persistence ------------------------------------------------

        def _load(self, path):
            with open(path, "r", encoding="utf-8-sig") as fh:
                return json.load(fh)

        def save(self):
            candidate = copy.deepcopy(self.decl)
            try:
                theme_tool.validate_declaration(candidate)
            except theme_tool.ThemeDeclarationError as e:
                messagebox.showerror("Cannot save", str(e))
                self.status.configure(text="Not saved: {}".format(e), foreground="#b00020")
                return
            with open(self.path, "w", encoding="utf-8", newline="\n") as fh:
                json.dump(self.decl, fh, indent=2)
                fh.write("\n")
            self.status.configure(text="Saved {}".format(self.path), foreground="#1a7f37")

        # --- theme list ---------------------------------------------------

        def _build_layout(self):
            toolbar = ttk.Frame(self.root)
            toolbar.pack(side="top", fill="x", padx=6, pady=4)
            ttk.Button(toolbar, text="Save", command=self.save).pack(side="left")
            self.status = ttk.Label(toolbar, text="")
            self.status.pack(side="left", padx=12)

            body = ttk.Frame(self.root)
            body.pack(side="top", fill="both", expand=True, padx=6, pady=(0, 6))

            left = ttk.Frame(body, width=220)
            left.pack(side="left", fill="y")
            left.pack_propagate(False)

            self.theme_list = tk.Listbox(left, exportselection=False)
            self.theme_list.pack(side="top", fill="both", expand=True)
            self.theme_list.bind("<<ListboxSelect>>", self._on_theme_selected)

            btns = ttk.Frame(left)
            btns.pack(side="top", fill="x", pady=4)
            ttk.Button(btns, text="Add", command=self.add_theme).pack(side="left", expand=True, fill="x")
            ttk.Button(btns, text="Duplicate", command=self.duplicate_theme).pack(side="left", expand=True, fill="x")
            ttk.Button(btns, text="Delete", command=self.delete_theme).pack(side="left", expand=True, fill="x")
            btns2 = ttk.Frame(left)
            btns2.pack(side="top", fill="x")
            ttk.Button(btns2, text="Rename", command=self.rename_theme).pack(side="left", expand=True, fill="x")
            ttk.Button(btns2, text="Set default", command=self.set_default_theme).pack(side="left", expand=True, fill="x")

            middle = ttk.Frame(body)
            middle.pack(side="left", fill="both", expand=True, padx=(8, 8))

            notebook = ttk.Notebook(middle)
            notebook.pack(side="top", fill="both", expand=True)

            colors_tab = ScrollableFrame(notebook)
            metrics_tab = ScrollableFrame(notebook)
            fonts_tab = ScrollableFrame(notebook)
            notebook.add(colors_tab, text="Colors")
            notebook.add(metrics_tab, text="Metrics")
            notebook.add(fonts_tab, text="Fonts")

            self.color_rows = [ColorRow(colors_tab.body, self, role, i) for i, role in enumerate(self.color_roles)]
            self.metric_rows = [MetricRow(metrics_tab.body, self, name, i) for i, name in enumerate(METRIC_FIELDS)]
            self.font_rows = [FontRow(fonts_tab.body, self, role, i) for i, role in enumerate(self.font_roles)]

            # A persistent side panel rather than a tab, so it stays visible
            # while a field on another tab is being edited.
            right = ttk.Frame(body, width=300)
            right.pack(side="left", fill="y")
            right.pack_propagate(False)
            ttk.Label(right, text="Preview", font=("Segoe UI", 10, "bold")).pack(anchor="w", pady=(0, 4))
            self.preview_canvas = tk.Canvas(right, width=280, height=560, background="#202020", highlightthickness=0)
            self.preview_canvas.pack(fill="both", expand=True)

        def refresh_theme_list(self):
            self.theme_list.delete(0, "end")
            for entry in self.decl.get("themes") or []:
                label = entry.get("name", "?")
                if entry.get("default"):
                    label = "* " + label
                self.theme_list.insert("end", label)

        def _on_theme_selected(self, _event):
            selection = self.theme_list.curselection()
            if selection:
                self.select_theme(selection[0])

        def select_theme(self, index):
            themes = self.decl.get("themes") or []
            if not themes:
                return
            self.theme_index = max(0, min(index, len(themes) - 1))
            self.theme_list.selection_clear(0, "end")
            self.theme_list.selection_set(self.theme_index)
            self.refresh_fields()

        def current_entry(self):
            return self.decl["themes"][self.theme_index]

        def is_current_default(self):
            return self.current_entry().get("default", False)

        # --- field edits, called by the row widgets -----------------------

        def write_field(self, category, key, value):
            set_own_field(self.current_entry(), category, key, value)
            self.refresh_preview()

        def clear_field(self, category, key):
            clear_own_field(self.current_entry(), category, key)
            self.refresh_fields()

        def refresh_fields(self):
            entry = self.current_entry()
            is_default = self.is_current_default()
            for row in self.color_rows:
                row.refresh(self.decl, entry, is_default)
            for row in self.metric_rows:
                row.refresh(self.decl, entry, is_default)
            for row in self.font_rows:
                row.refresh(self.decl, entry, is_default)
            self.refresh_preview()

        def refresh_preview(self):
            style = preview_style(self.decl, self.current_entry())
            colors = style["colors"]
            metrics = style["metrics"]
            c = self.preview_canvas
            c.delete("all")
            canvas_w = int(c.cget("width"))
            canvas_h = int(c.cget("height"))

            def hexc(role, fallback="#404040"):
                v = colors.get(role)
                return rgba_to_hex(v) if v else fallback

            pad = int(metrics.get("panelPadding", 8))
            item_spacing = int(metrics.get("itemSpacing", 4))
            row_padding = int(metrics.get("rowPadding", 3))
            border = max(1, int(metrics.get("borderWidth", 1)))
            bar_height = max(4, int(metrics.get("barHeight", 8)))
            cursor_size = max(4, int(metrics.get("cursorSize", 10)))
            scroll_w = max(2, int(metrics.get("scrollBarWidth", 6)))
            row_h = 20 + row_padding * 2

            c.create_rectangle(0, 0, canvas_w, canvas_h, fill=hexc("WindowBackground", "#101010"), outline="")

            margin = 14
            x0, y0 = margin, margin
            x1, y1 = canvas_w - margin - scroll_w - 4, canvas_h - margin
            c.create_rectangle(x0, y0, x1, y1, fill=hexc("PanelBackground"), outline=hexc("Border"), width=border)

            content_x = x0 + pad
            content_r = x1 - pad
            y = y0 + pad
            c.create_text(content_x, y, anchor="nw", text="Panel title", fill=hexc("Header", "#ffffff"), font=("Segoe UI", 11, "bold"))
            y += 22
            c.create_line(content_x, y, content_r, y, fill=hexc("Border"))
            y += item_spacing + 4

            def row(label, fill_role, outline_role=None, text_role="Text", marker=""):
                nonlocal y
                c.create_rectangle(content_x, y, content_r, y + row_h, fill=hexc(fill_role), outline=hexc(outline_role) if outline_role else "", width=border)
                c.create_text(content_x + 8, y + row_h / 2, anchor="w", text=marker + label, fill=hexc(text_role, "#ffffff"))
                y += row_h + item_spacing

            row("A button", "ItemBackground")
            row("Hovered button", "ItemHovered", outline_role="Focus")
            row("Selected row", "ItemActive", text_role="TextAccent", marker="> ")

            # Checkbox: an outlined box with a fill mark when "checked".
            box = row_h - 8
            c.create_rectangle(content_x, y + 4, content_x + box, y + 4 + box, fill=hexc("ItemBackground"), outline=hexc("Border"), width=border)
            c.create_line(content_x + 3, y + 4 + box / 2, content_x + box / 2, y + box, content_x + box - 2, y + 6, fill=hexc("BarFill", "#4caf50"), width=2)
            c.create_text(content_x + box + 8, y + row_h / 2, anchor="w", text="A checkbox", fill=hexc("Text", "#ffffff"))
            y += row_h + item_spacing * 2

            for label, role, fallback in (
                ("Body text reads like this.", "Text", "#ffffff"),
                ("Dimmed text reads like this.", "TextDim", "#a0a0a0"),
                ("Accent text reads like this.", "TextAccent", "#ffcc00"),
                ("Warning text reads like this.", "TextWarn", "#ff8060"),
                ("Disabled text reads like this.", "TextDisabled", "#555555"),
            ):
                c.create_text(content_x, y, anchor="nw", text=label, fill=hexc(role, fallback))
                y += 18
            y += item_spacing

            # Two bars: a normal fill and a warn fill, both on the same track colour.
            for label, fill_role, fraction in (("Progress", "BarFill", 0.7), ("Low on space", "BarFillWarn", 0.25)):
                c.create_text(content_x, y, anchor="nw", text=label, fill=hexc("Text", "#ffffff"))
                y += 16
                bar_w = content_r - content_x
                c.create_rectangle(content_x, y, content_x + bar_w, y + bar_height, fill=hexc("BarTrack"), outline="")
                c.create_rectangle(content_x, y, content_x + bar_w * fraction, y + bar_height, fill=hexc(fill_role, "#4caf50"), outline="")
                y += bar_height + item_spacing

            # A cursor, resting over the row above.
            cx, cy = content_x + 40, y - bar_height - item_spacing - 10
            c.create_oval(cx, cy, cx + cursor_size, cy + cursor_size, fill=hexc("Cursor", "#ffffff"), outline=hexc("CursorOutline", "#000000"), width=2)

            # A scrollbar track and thumb along the panel's own right edge.
            sb_x = x1 + 4
            c.create_rectangle(sb_x, y0, sb_x + scroll_w, y1, fill=hexc("BarTrack"), outline="")
            c.create_rectangle(sb_x, y0, sb_x + scroll_w, y0 + (y1 - y0) * 0.4, fill=hexc("Border"), outline="")

        # --- theme list actions --------------------------------------------

        def _prompt_name(self, title, initial=""):
            while True:
                name = simpledialog.askstring(title, "Theme name (upper snake case):", initialvalue=initial, parent=self.root)
                if name is None:
                    return None
                if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name):
                    messagebox.showerror(title, "Name must be upper snake case, e.g. MIDNIGHT.")
                    continue
                if any(e.get("name") == name for e in self.decl.get("themes") or []):
                    messagebox.showerror(title, "A theme named {} already exists.".format(name))
                    continue
                return name

        def add_theme(self):
            name = self._prompt_name("Add theme")
            if name is None:
                return
            self.decl.setdefault("themes", []).append(new_theme_entry(name))
            self.refresh_theme_list()
            self.select_theme(len(self.decl["themes"]) - 1)

        def duplicate_theme(self):
            name = self._prompt_name("Duplicate theme", initial=self.current_entry().get("name", "") + "_COPY")
            if name is None:
                return
            clone = copy.deepcopy(self.current_entry())
            clone["name"] = name
            clone["default"] = False
            self.decl["themes"].append(clone)
            self.refresh_theme_list()
            self.select_theme(len(self.decl["themes"]) - 1)

        def delete_theme(self):
            themes = self.decl.get("themes") or []
            if len(themes) <= 1:
                messagebox.showerror("Delete theme", "A declaration needs at least one theme.")
                return
            if self.is_current_default():
                messagebox.showerror("Delete theme", "Mark another theme as default before deleting this one.")
                return
            if not messagebox.askyesno("Delete theme", "Delete '{}'?".format(self.current_entry().get("name"))):
                return
            del themes[self.theme_index]
            self.refresh_theme_list()
            self.select_theme(min(self.theme_index, len(themes) - 1))

        def rename_theme(self):
            name = self._prompt_name("Rename theme", initial=self.current_entry().get("name", ""))
            if name is None:
                return
            self.current_entry()["name"] = name
            self.refresh_theme_list()
            self.select_theme(self.theme_index)

        def set_default_theme(self):
            for entry in self.decl.get("themes") or []:
                entry["default"] = False
            self.current_entry()["default"] = True
            self.refresh_theme_list()
            self.select_theme(self.theme_index)

    root = tk.Tk()
    ThemeEditorApp(root, path)
    root.mainloop()


def main(argv=None):
    parser = argparse.ArgumentParser(description="Visual editor for a theme declaration")
    parser.add_argument("--theme-path", default=theme_tool.DEFAULT_DECLARATION)
    args = parser.parse_args(argv)

    if not os.path.isfile(args.theme_path):
        print("theme_editor: {} does not exist".format(args.theme_path), file=sys.stderr)
        return 1

    try:
        _run_app(args.theme_path)
    except ImportError as e:
        if "_tkinter" not in str(e):
            raise
        print(
            "theme_editor: this Python has no Tk support ({}). On Windows, "
            "devkitPro's bundled MSYS2 python3 lacks it and has no package to "
            "add it -- use the Python Launcher instead: py -3 tools/theme_editor.py".format(e),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
