# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
"""Private-display initialization/child inheritance, no package installation."""
import os
import ctypes
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
keys = ('GSK_RENDERER', 'GDK_DISABLE', 'GDK_DEBUG')
runtime = ctypes.CDLL('libgtk-4.so.1')
modern = (runtime.gtk_get_major_version(), runtime.gtk_get_minor_version()) >= (4, 16)
case = sys.argv[1]
values = dict.fromkeys(keys)
if case == 'empty': values = dict.fromkeys(keys, '')
if case == 'ngl': values['GSK_RENDERER'] = 'ngl'
if case == 'flags':
    values['GDK_DISABLE'] = 'dmabuf' if modern else None
    values['GDK_DEBUG'] = 'selection'
if case in ('all', 'except'):
    values['GDK_DISABLE' if modern else 'GDK_DEBUG'] = ('all' if case == 'all' else
        ('ALL:GL;vulkan,dmabuf' if modern else 'ALL:gl-disable;vulkan-disable,selection'))
for key, value in values.items():
    if value is None: os.environ.pop(key, None)
    else: os.environ[key] = value
import app
from gi.repository import Gdk, Gtk, Gsk
app.init_gui(); app.init_gui()
assert {key: os.environ.get(key) for key in keys} == values
try:
    prepared = Gdk.Display.get_default().prepare_gl()
except app.GLib.Error:
    prepared = False
assert not prepared
for _ in range(3):
    window = Gtk.Window(); window.set_child(Gtk.Entry()); window.realize()
    assert isinstance(window.get_renderer(), Gsk.CairoRenderer)
    window.destroy()
child = subprocess.check_output([sys.executable, '-c',
    'import json,os; print(json.dumps({k:os.environ.get(k) for k in '
    ' ("GSK_RENDERER","GDK_DISABLE","GDK_DEBUG")}))'], text=True)
assert app.json.loads(child) == values
print('PASS: Python Cairo/no-GL initialization, repeated windows and child environment')
