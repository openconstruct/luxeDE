#!/usr/bin/python3
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
import json
import ctypes
import os
import re
from pathlib import Path
import shutil
import sys
import tempfile
import threading
import gi
gi.require_version('Gtk', '4.0')
gi.require_version('Gdk', '4.0')
gi.require_version('Gsk', '4.0')
gi.require_version('Vte', '3.91')
_gui_initialized = False

def init_gui():
    global _gui_initialized, Gtk, Gdk, Gsk, Gio, GLib, Vte, Pango
    if _gui_initialized:
        return
    # PyGObject's Gtk override initializes GTK while importing it. Set the
    # process-local flags BEFORE that import, not only before explicit init.
    # Version getters do not initialize a display; use the loaded runtime ABI.
    runtime = ctypes.CDLL('libgtk-4.so.1')
    modern = (runtime.gtk_get_major_version(), runtime.gtk_get_minor_version()) >= (4, 16)
    key = 'GDK_DISABLE' if modern else 'GDK_DEBUG'
    graphics = ('gl', 'vulkan') if modern else ('gl-disable', 'vulkan-disable')
    originals = {name: os.environ.get(name) for name in ('GSK_RENDERER', key)}
    tokens = [token for token in re.split(r'[:;,\s]+', originals[key] or '') if token]
    if any(token.lower() == 'all' for token in tokens):
        tokens = [token for token in tokens if token.lower() not in graphics]
    else:
        tokens.extend(graphics)
    os.environ[key] = ','.join(tokens)
    os.environ['GSK_RENDERER'] = 'cairo'
    try:
        from gi.repository import Gtk, Gdk, Gsk, Gio, GLib, Vte, Pango
        Gtk.init()
        window = Gtk.Window()
        window.realize()
        if not isinstance(window.get_renderer(), Gsk.CairoRenderer):
            raise RuntimeError('Could not initialize the Cairo renderer')
        window.destroy()
        _gui_initialized = True
    finally:
        for name, value in originals.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value

init_gui()

HERE = Path(__file__).resolve().parent
# -I excludes the script directory; only add our trusted installed location.
sys.path.insert(0, str(HERE))
try:
    from paths import HELPER, VERSION
    from process_runner import run_bounded
    from snapshot_copy import copy_snapshot
except ModuleNotFoundError:
    HELPER, VERSION = '/usr/libexec/debinstall-install', 'development'
    from process_runner import run_bounded
    from snapshot_copy import copy_snapshot

MAX_INSPECTION_OUTPUT_BYTES = 1024 * 1024

def parse_inspection_output(output):
    """Return JSON from the last non-blank line of helper output."""
    lines = [line for line in (output or '').splitlines() if line.strip()]
    if not lines:
        raise ValueError('Package inspection produced no output.')
    try:
        return json.loads(lines[-1])
    except json.JSONDecodeError as error:
        raise ValueError(f'Package inspection returned invalid JSON: {error}') from error

def inspect_snapshot(snapshot, cancel_event):
    """Run the planner with bounded output, close cancellation, and a deadline."""
    args = ['/usr/bin/python3', '-I', str(HERE/'backend.py'), str(snapshot)]
    return run_bounded(
        args,
        timeout=120,
        max_output_bytes=MAX_INSPECTION_OUTPUT_BYTES,
        cancel_event=cancel_event,
    )

CREDITS = ('Copyright © 2026 Jerry Howell\n\nPowered by gdebi-core\n'
           'Copyright © 2005–2013 Canonical Ltd\nCopyright © 2004 Martin Böhm\n'
           'Copyright © 2009–2013 Luca Falavigna\n\nGDebi author: Michael Vogt\n'
           'Additional upstream attribution: Sandino Flores Moreno (2004), '
           'SimpleGtkbuilderApp, LGPL-2.1. That GTK frontend is not included in DebInstall.')

def label(text='', css=None):
    widget = Gtk.Label(label=text, xalign=0)
    widget.set_wrap(True)
    widget.set_selectable(True)
    if css:
        widget.add_css_class(css)
    return widget

def text_page():
    text = Gtk.TextView(editable=False, cursor_visible=False, monospace=True,
                        wrap_mode=Gtk.WrapMode.WORD_CHAR)
    text.set_left_margin(12)
    text.set_right_margin(12)
    text.set_top_margin(12)
    text.set_bottom_margin(12)
    scroll = Gtk.ScrolledWindow(vexpand=True)
    scroll.set_child(text)
    return scroll, text.get_buffer()

class Window(Gtk.ApplicationWindow):
    def __init__(self, app, path=None):
        super().__init__(application=app, title='DebInstall', default_width=730, default_height=660)
        self.set_icon_name('debinstall')
        self.owner_app = app
        self.data = None
        self.snapshot = None
        self.cache = None
        self.busy = False
        self.installing = False
        self.closed = False
        self.success = False
        self.copy_cancel = None
        self.connect('close-request', self.close_request)
        header = Gtk.HeaderBar()
        self.open_button = Gtk.Button(label='Open…', tooltip_text='Open a Debian package (Ctrl+O)')
        self.open_button.connect('clicked', self.choose)
        header.pack_start(self.open_button)
        about = Gtk.Button(icon_name='help-about-symbolic', tooltip_text='About DebInstall')
        about.connect('clicked', self.about)
        header.pack_end(about)
        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        for edge in ('top', 'bottom', 'start', 'end'):
            getattr(root, 'set_margin_' + edge)(10)
        self.set_titlebar(header)
        self.set_child(root)
        self.stack = Gtk.Stack(vexpand=True)
        root.append(self.stack)
        empty = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14,
                        valign=Gtk.Align.CENTER, halign=Gtk.Align.CENTER)
        icon = Gtk.Image.new_from_icon_name('debinstall')
        icon.set_pixel_size(80)
        empty.append(icon)
        title = Gtk.Label(label='Install a Debian package')
        title.add_css_class('title-1')
        empty.append(title)
        empty.append(Gtk.Label(label='Review a local .deb and its dependencies before installing.'))
        start = Gtk.Button(label='Open a .deb file', halign=Gtk.Align.CENTER)
        start.add_css_class('suggested-action')
        start.connect('clicked', self.choose)
        empty.append(start)
        self.stack.add_named(empty,'empty')
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        self.name = label('', 'title-1')
        self.name.set_selectable(False)
        page.append(self.name)
        self.subtitle = label('', 'dim-label')
        page.append(self.subtitle)
        self.filename = label('', 'dim-label')
        self.filename.set_ellipsize(Pango.EllipsizeMode.MIDDLE)
        self.filename.set_wrap(False)
        page.append(self.filename)
        self.notebook = Gtk.Notebook(vexpand=True)
        page.append(self.notebook)
        details = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        for edge in ('top', 'bottom', 'start', 'end'):
            getattr(details, 'set_margin_' + edge)(10)
        self.description = label()
        details.append(self.description)
        self.metadata = label()
        details.append(self.metadata)
        scroll = Gtk.ScrolledWindow(vexpand=True)
        scroll.set_child(details)
        self.notebook.append_page(scroll, Gtk.Label(label='Details'))
        changes, self.changes = text_page()
        self.notebook.append_page(changes, Gtk.Label(label='Changes'))
        files, self.files = text_page()
        self.notebook.append_page(files, Gtk.Label(label='Contents'))
        self.stack.add_named(page,'package')
        self.output = Gtk.Expander(label='Installation output', expanded=False)
        self.terminal = Vte.Terminal()
        self.terminal.set_scrollback_lines(10000)
        self.terminal.set_font(Pango.FontDescription.from_string('Monospace 10'))
        self.terminal.connect('child-exited', self.child_exited)
        term_scroll = Gtk.ScrolledWindow(min_content_height=190)
        term_scroll.set_child(self.terminal)
        self.output.set_child(term_scroll)
        self.output.set_visible(False)
        root.append(self.output)
        footer = Gtk.Box(spacing=6)
        self.spinner = Gtk.Spinner()
        footer.append(self.spinner)
        self.status = label('Powered by gdebi-core')
        self.status.set_hexpand(True)
        footer.append(self.status)
        self.install_button = Gtk.Button(label='Install', valign=Gtk.Align.CENTER, sensitive=False)
        self.install_button.add_css_class('suggested-action')
        self.install_button.connect('clicked', self.primary_action)
        footer.append(self.install_button)
        root.append(footer)
        action = Gio.SimpleAction.new('open', None)
        action.connect('activate', lambda *_: self.choose())
        self.add_action(action)
        if path:
            self.open_path(path)

    def state(self, text, busy=False, install=False, error=False):
        self.busy = busy
        self.status.set_text(text)
        self.status.remove_css_class('error')
        if error:
            self.status.add_css_class('error')
        self.open_button.set_sensitive(not busy)
        self.install_button.set_sensitive(install and not busy)
        if busy:
            self.spinner.start()
        else:
            self.spinner.stop()

    def choose(self, *_):
        if self.busy:
            return
        dialog = Gtk.FileChooserNative.new(
            'Open Debian package',
            self,
            Gtk.FileChooserAction.OPEN,
            'Open',
            'Cancel',
        )
        filt = Gtk.FileFilter()
        filt.set_name('Debian packages (*.deb)')
        filt.add_pattern('*.deb')
        dialog.add_filter(filt)

        def response(chooser, result):
            file = chooser.get_file() if result == Gtk.ResponseType.ACCEPT else None
            chooser.destroy()
            if file:
                if file.get_path():
                    self.open_path(file.get_path())
                else:
                    self.state('Download the package to a local folder first.', error=True)
        dialog.connect('response', response)
        dialog.show()

    def open_path(self, path):
        if self.busy:
            return
        self.cleanup()
        self.data = None
        self.success = False
        self.install_button.set_label('Install')
        self.install_button.remove_css_class('destructive-action')
        self.install_button.add_css_class('suggested-action')
        self.output.set_visible(False)
        self.terminal.reset(True, True)
        self.stack.set_visible_child_name('empty')
        self.state('Reading package and checking dependencies…', busy=True)
        self.owner_app.hold()  # allow a closing window's worker to clean its private snapshot
        cancel_event = threading.Event()
        self.copy_cancel = cancel_event

        def worker():
            directory = None
            try:
                source = Path(path).expanduser().resolve(strict=True)
                directory = tempfile.mkdtemp(prefix='debinstall-')
                snapshot = Path(directory) / 'package.deb'
                copy_snapshot(source, snapshot, cancel_event)
                snapshot.chmod(0o600)
                run = inspect_snapshot(snapshot, cancel_event)
                try:
                    data = parse_inspection_output(run.stdout)
                except ValueError as parse_error:
                    raise RuntimeError(run.stderr.strip() or str(parse_error))
                if 'error' in data:
                    raise ValueError(data['error'])
                if run.returncode:
                    raise RuntimeError(run.stderr.strip() or 'Package inspection failed.')
                GLib.idle_add(self.loaded, data, str(source), directory)
            except Exception as error:
                if directory:
                    shutil.rmtree(directory, ignore_errors=True)
                GLib.idle_add(self.load_failed, str(error))
        threading.Thread(target=worker, daemon=True).start()

    def load_failed(self, error):
        self.owner_app.release()
        if not self.closed:
            self.state('Could not open package: ' + error, error=True)
        return False

    def loaded(self, data, source, directory):
        self.owner_app.release()
        if self.closed:
            shutil.rmtree(directory, ignore_errors=True)
            return False
        self.data = data
        self.cache = directory
        self.snapshot = str(Path(directory) / 'package.deb')
        self.name.set_text(data['package'])
        installed = ' · Installed: ' + data['installed'] if data['installed'] else ''
        self.subtitle.set_text(f"{data['version']} · {data['architecture']}{installed}")
        self.filename.set_text(source)
        self.filename.set_tooltip_text(source)
        self.description.set_text(data['description'].replace('\n .\n', '\n\n'))
        metadata = [
            'Maintainer: ' + (data['maintainer'] or 'Not specified'),
            'Homepage: ' + (data['homepage'] or 'Not specified'),
            'Installed size: ' + (data['installed-size'] + ' KiB' if data['installed-size'] else 'Not specified'),
            'Dependencies: ' + (data['depends'] or 'None'),
        ]
        if data['pre-depends']:
            metadata.append('Pre-dependencies: ' + data['pre-depends'])
        self.metadata.set_text('\n'.join(metadata))

        lines = [f"Install local package: {data['package']} {data['version']}"]
        lines.extend(
            f"{change['action']}: {change['name']} {change['version']}"
            for change in data['changes']
        )
        if not data['changes']:
            lines.append('\nNo additional package changes are required.')
        if not data['ready']:
            lines.extend(['', 'Cannot install: ' + data['failure']])
        self.changes.set_text('\n'.join(lines))
        files = list(data['files'])
        hidden = data.get('files_truncated', 0)
        if hidden and not (files and files[-1].startswith('[…')):
            files.append(f'[… {hidden} more files not shown]')
        self.files.set_text('\n'.join(files))
        self.stack.set_visible_child_name('package')
        self.install_button.set_label(
            'Reinstall' if data['version'] == data['installed'] else 'Install'
        )
        self.install_button.remove_css_class('destructive-action')
        self.install_button.add_css_class('suggested-action')
        remove_count = sum(change['action'] == 'Remove' for change in data['changes'])
        status = f"Ready. {len(data['changes'])} dependency changes"
        status += f", including {remove_count} removals." if remove_count else '.'
        self.state(
            status if data['ready'] else data['failure'],
            install=data['ready'],
            error=not data['ready'],
        )
        if remove_count:
            self.notebook.set_current_page(1)
        return False

    def primary_action(self, *_):
        if self.success:
            self.close()
            return
        self.confirm_install()

    def confirm_install(self, *_):
        if self.busy or not self.data or not self.data['ready']:
            return
        removals = [
            change['name'] for change in self.data['changes']
            if change['action'] == 'Remove'
        ]
        detail = 'This installs software system-wide. Authentication is required.'
        if removals:
            detail += '\n\nPackages to remove:\n' + ', '.join(removals)
        dialog = Gtk.AlertDialog(
            message=f"Install {self.data['package']} {self.data['version']}?",
            detail=detail,
            buttons=['Cancel', 'Install'],
            cancel_button=0,
            default_button=0,
        )
        reviewed = self.data
        previous_status = self.status.get_text()
        self.state('Waiting for confirmation…', busy=True)
        def finished(source, result):
            try:
                choice = source.choose_finish(result)
            except GLib.Error:
                choice = 0
            if self.closed:
                return
            self.state(previous_status, install=True)
            if choice == 1 and self.data is reviewed:
                self.start_install()

        dialog.choose(self, None, finished)

    def start_install(self):
        if not Path(HELPER).is_file():
            self.state(
                'Install the DebInstall DEB first to enable authenticated installation.',
                install=True,
                error=True,
            )
            return
        self.installing = True
        self.state('Authenticating and installing…', busy=True)
        self.output.set_visible(True)
        self.output.set_expanded(True)
        # Authentication belongs in the desktop agent. Without this flag,
        # pkexec falls back to a password prompt inside the embedded terminal
        # whenever an agent is temporarily unavailable.
        argv = [
            '/usr/bin/pkexec', '--disable-internal-agent', HELPER, self.snapshot,
            self.data['sha256'], self.data['plan_token'],
        ]
        self.terminal.spawn_async(
            Vte.PtyFlags.DEFAULT,
            None,
            argv,
            None,
            GLib.SpawnFlags.DEFAULT,
            None,
            None,
            -1,
            None,
            self.spawned,
            None,
        )

    def spawned(self, terminal, pid, error, *_):
        if error:
            self.installing = False
            self.state('Could not start installation: ' + str(error), install=True, error=True)

    def child_exited(self,terminal,status):
        if not self.installing:
            return
        self.installing=False
        try:
            GLib.spawn_check_wait_status(status)
            success = True
        except GLib.Error:
            success = False
        self.success=success
        if success:
            self.state('Installation complete.')
            self.install_button.set_label('Close')
            self.install_button.remove_css_class('suggested-action')
            self.install_button.remove_css_class('destructive-action')
            self.install_button.set_sensitive(True)
        else:
            self.state('Installation cancelled or failed. Review the output; reopen the package to retry.',error=True)

    def about(self, *_):
        dialog = Gtk.AboutDialog(
            program_name='DebInstall',
            version=VERSION,
            logo_icon_name='debinstall',
            comments='A GTK4 Debian package installer powered by gdebi-core.',
            website='https://github.com/openconstruct/luxeDE',
            copyright=CREDITS,
            license_type=Gtk.License.GPL_2_0,
            authors=[
                'Jerry Howell',
                'gdebi-core contributors: Michael Vogt, Canonical Ltd, Martin Böhm, Luca Falavigna',
            ],
            transient_for=self,
            modal=True,
            destroy_with_parent=True,
        )
        dialog.present()

    def cleanup(self):
        if self.cache:
            shutil.rmtree(self.cache, ignore_errors=True)
        self.cache = None
        self.snapshot = None

    def close_request(self, *_):
        if self.installing:
            self.status.set_text('Installation is running. Wait for it to finish before closing.')
            return True
        if self.copy_cancel:
            self.copy_cancel.set()
        self.closed = True
        self.cleanup()
        return False

class Application(Gtk.Application):
    def __init__(self):
        init_gui()
        GLib.set_prgname('debinstall')
        GLib.set_application_name('DebInstall')
        super().__init__(
            application_id='org.lumen.DebInstall',
            flags=Gio.ApplicationFlags.HANDLES_OPEN,
        )
        self.set_accels_for_action('win.open',['<Control>o'])

    def do_activate(self):
        window = self.get_active_window() or Window(self)
        window.present()
    def do_open(self, files, hint_count, hint):
        for file in files:
            window = Window(self, file.get_path())
            if not file.get_path():
                window.state('Download the package to a local folder first.', error=True)
            window.present()

if __name__=='__main__':
    sys.exit(Application().run(sys.argv))
