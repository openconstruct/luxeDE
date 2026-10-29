# Frame

GTK4/Cairo image viewer and Luxe wallpaper owner. Wallpaper CLI is handled
before GTK initialization: GLib owns configuration/IPC/rotation, X11/Cairo owns
the backdrop, and image buffers are released after painting. The viewer and
folder-selection dialog remain GTK4. This is an X11 desktop integration path,
not a claim of already implemented Wayland background placement.

- `frame --wallpaper-daemon`: start/activate the singleton wallpaper service.
- `frame --wallpaper-settings`: choose the wallpaper directory.
- `frame --wallpaper-directory /path`: select a folder of images.
- `frame --set-wallpaper /path/image.png`: select one image (including from the viewer or theme picker).
- `frame --wallpaper-stop`: stop the backdrop service.

Images stretch over the full X11 root. A folder with multiple supported images
advances in sorted filename order once per 24-hour deadline or at next login,
whichever occurs first. Invalid images are skipped. A single image has no
rotation timer, except that a bundled orbit image selects the active Luxe
theme's orbit, Horizon, River and Veil images when those files are installed.
These variants rotate on the same schedule; a missing collection is skipped.
The active theme is read from Appearance's saved state on each selection; if
there is no recognized theme, the selected orbit's palette is used. Custom
images and folders retain their explicit selection behavior. The service
listens for RandR changes, not a redraw poll.
Settings live in `~/.config/luxe/wallpaper.ini`; existing LXDE PCManager wallpaper
is read once for migration. PCManager no longer decodes or paints wallpapers.

The original file stays selected and untouched. Each successful apply prepares
an exact desktop-sized raster (1280x720 on a 720p desktop, 3840x2160 on 4K) before
X11 upload. RandR changes prepare it again from the original. Multi-monitor
stretch covers the combined X11 root, as before. One lossless PNG derivative
lives at `$XDG_CACHE_HOME/frame/wallpaper.png` (normally
`~/.cache/frame/wallpaper.png`), compression level 7, atomically replaced rather
than accumulating per-image copies. Native GdkPixbuf handles this; ImageMagick
is not required. An unavailable/unsafe cache does not prevent painting, and
selecting the derivative itself never overwrites that selected source.
Resizing reduces retained upload RAM, not PNG compression. PNG preparation
costs CPU/disk on apply, never an idle timer. Target hardware savings remain
unverified; see `artifacts/FRAME-WALLPAPER-MEMORY.md` at the repository root.

Bounds: 4096 directory entries (the first entries in filesystem order; a warning
is logged if more exist), 1 MiB settings, 256 MiB encoded image, 64 million
source pixels and 4096-pixel decoded edge for both viewer and wallpaper. These
limits do not bound every format loader's intermediate memory or CPU. Failed
decoding preserves the current backdrop. GUI decoding and WebP/EXIF/viewer
behavior remain covered by tests.

Image information and the status bar report original file pixel dimensions
before EXIF orientation or temporary view rotation. Zoom for an image reduced
to the display-size limit is labeled relative to its preview.
