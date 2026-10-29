# Copyright 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
import subprocess
import sys
import threading
import time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'src'))
import app
from gi.repository import Gtk,GLib,Vte,Gsk
application=app.Application();assert isinstance(application,Gtk.Application)
application.register(None)
window=app.Window(application,sys.argv[1]);assert isinstance(window,Gtk.ApplicationWindow);window.present()
def pump(seconds=.1):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        while GLib.MainContext.default().iteration(False):pass
        time.sleep(.01)
for _ in range(200):
    pump()
    if not window.busy:break
assert window.data,window.status.get_text()
assert window.data['ready'],window.status.get_text()
assert window.install_button.get_sensitive()

# Closing while the snapshot worker is reading must signal cancellation and
# release its private cache directory without waiting for the read to finish.
copy_started = threading.Event()
copy_cancelled = threading.Event()

def wait_for_copy_cancel(_source, _destination, cancel_event):
    copy_started.set()
    if cancel_event.wait(3):
        copy_cancelled.set()
    raise RuntimeError('Package copy was cancelled.')

original_copy_snapshot = app.copy_snapshot
app.copy_snapshot = wait_for_copy_cancel
cancel_window = app.Window(application)
cancel_window.present()
cancel_window.open_path(sys.argv[1])
for _ in range(100):
    pump(0.02)
    if copy_started.is_set():
        break
assert copy_started.is_set()
cancel_window.close()
for _ in range(100):
    pump(0.02)
    if copy_cancelled.is_set():
        break
app.copy_snapshot = original_copy_snapshot
assert copy_cancelled.is_set()

# Capture the Gtk.AlertDialog app.py presents (rather than probing the
# toolkit's internal dialog widget) so cancellation can be exercised below.
shown_alerts=[]
OrigAlertChoose=Gtk.AlertDialog.choose
def spy_choose(self,*args,**kwargs):
    shown_alerts.append(self);return OrigAlertChoose(self,*args,**kwargs)
Gtk.AlertDialog.choose=spy_choose
try:window.confirm_install();pump()
finally:Gtk.AlertDialog.choose=OrigAlertChoose
# app.py confirms via Gtk.AlertDialog: assert at that API level instead of
# relying on the toolkit's internal presentation widget.
assert len(shown_alerts)==1 and isinstance(shown_alerts[0],Gtk.AlertDialog),shown_alerts
assert shown_alerts[0].get_message()==f"Install {window.data['package']} {window.data['version']}?"
assert list(shown_alerts[0].get_buttons())==['Cancel','Install']
assert shown_alerts[0].get_cancel_button()==0
assert window.busy
reviewed = window.data
window.open_path('/nonexistent/replacement.deb')
window.confirm_install()
assert window.data is reviewed
# Dismiss the presented confirmation the way a user would, without touching
# dialog internals: closing the dialog cancels the installation.
model=Gtk.Window.get_toplevels()
extra=[model.get_item(i) for i in range(model.get_n_items()) if model.get_item(i) is not window]
assert len(extra)==1,extra
extra[0].close();pump();assert not window.installing and not window.busy
screenshot=Path(sys.argv[2])
subprocess.run(['/usr/bin/python3','-c',"import gi;gi.require_version('Gdk','3.0');from gi.repository import Gdk;root=Gdk.get_default_root_window();Gdk.pixbuf_get_from_window(root,0,0,1000,800).savev(__import__('sys').argv[1],'png',[],[])",str(screenshot)],check=True)
# Exercise the actual embedded-terminal callbacks without invoking pkexec or APT.
window.installing=True;window.state('Terminal test',busy=True);window.output.set_visible(True);window.output.set_expanded(True)
window.terminal.spawn_async(Vte.PtyFlags.DEFAULT,None,['/usr/bin/printf','Terminal test: no packages installed.\\n'],None,GLib.SpawnFlags.DEFAULT,None,None,-1,None,window.spawned,None)
for _ in range(100):
    pump()
    if not window.installing:break
assert window.success,window.status.get_text()
assert window.install_button.get_label()=='Close'
assert window.install_button.get_sensitive()
assert not window.install_button.has_css_class('destructive-action')
assert not window.install_button.has_css_class('suggested-action')
# Inspect the actual native dialog and renderer, not a patched presentation API.
assert isinstance(window.get_renderer(),Gsk.CairoRenderer)
window.about();pump()
shown_about=[w for w in Gtk.Window.get_toplevels() if isinstance(w,Gtk.AboutDialog) and w.get_visible()]
assert len(shown_about)==1,shown_about
about=shown_about[0]
_parent=about.get_transient_for()
assert about.get_modal()
assert about.get_program_name()=='DebInstall',about.get_program_name()
assert about.get_version()==app.VERSION,about.get_version()
assert about.get_comments()=='A GTK4 Debian package installer powered by gdebi-core.',about.get_comments()
assert about.get_website()=='https://github.com/openconstruct/luxeDE',about.get_website()
assert about.get_logo_icon_name()=='debinstall',about.get_logo_icon_name()
assert about.get_license_type()==Gtk.License.GPL_2_0
assert 'Jerry Howell' in list(about.get_authors()),about.get_authors()
assert _parent is window,(_parent,window)
about.close();pump()
window.install_button.emit('clicked');pump();assert window.closed
assert not window.cache
print('PASS: package details, install readiness, confirmation cancellation, successful Close action, embedded terminal exit, About and snapshot cleanup')
