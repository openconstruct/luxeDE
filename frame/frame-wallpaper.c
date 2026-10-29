/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#include "frame-wallpaper.h"
#include "frame-decode.h"
#include "luxe-gtk-ui.h"
#include "luxe-dialogs-gtk.h"
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <cairo-xlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/timerfd.h>
#include <stdint.h>
#endif
#ifdef __GLIBC__
#include <malloc.h>
#endif

#ifndef LUXE_WALLPAPER_DIR
#define LUXE_WALLPAPER_DIR "/usr/share/backgrounds/luxe"
#endif
#ifndef LUXE_DEFAULT_PALETTE
#error "LUXE_DEFAULT_PALETTE must come from LuxePaletteCatalog.cmake"
#endif
typedef struct {
    GApplication *app;
    Display *display;
    Pixmap pixmap;
    GKeyFile *config;
    char *path;
    guint rotation;
    int rotation_fd;
    GSource *events;
    int randr_event, width, height;
    gboolean held;
    GCancellable *prepare_cancel;
    guint64 request_generation;
    GPtrArray *files;
    guint active_preparations;
    gboolean stopping;
} Wallpaper;

static GMutex wallpaper_prepare_mutex;

static char *config_path(void)
{ return g_build_filename(g_get_user_config_dir(), "luxe", "wallpaper.ini", NULL); }

static char *appearance_theme(const char *config_dir)
{
    g_autofree char *path=g_build_filename(config_dir,"luxe","appearance.ini",NULL);
    struct stat st;
    if(g_lstat(path,&st) || !S_ISREG(st.st_mode) || st.st_uid!=getuid() || st.st_size>1024*1024)
        return NULL;
    g_autoptr(GKeyFile) state=g_key_file_new();
    if(!g_key_file_load_from_file(state,path,0,NULL)) return NULL;
    return g_key_file_get_string(state,"Appearance","theme",NULL);
}

static gboolean palette_id_is_safe(const char *id)
{
    if(!id || !*id) return FALSE;
    for(const unsigned char *p=(const unsigned char *)id;*p;p++)
        if(!g_ascii_isalnum(*p) && *p!='-') return FALSE;
    return TRUE;
}

static char *default_wallpaper_path(const char *root,const char *config_dir)
{
    g_autofree char *theme=appearance_theme(config_dir);
    /* appearance.ini is user-writable. Treat its palette value as an asset
     * identifier, never as a path component supplied by the user. */
    if(!palette_id_is_safe(theme)) {
        g_clear_pointer(&theme,g_free);
        theme=g_strdup(LUXE_DEFAULT_PALETTE);
    }
    g_autofree char *name=g_strdup_printf("%s.png",theme);
    g_autofree char *path=g_build_filename(root,name,NULL);
    if(g_file_test(path,G_FILE_TEST_IS_REGULAR)) return g_steal_pointer(&path);
    g_free(g_steal_pointer(&name));
    name=g_strdup_printf("%s.png",LUXE_DEFAULT_PALETTE);
    return g_build_filename(root,name,NULL);
}

static gboolean save(Wallpaper *w, GError **error)
{
    g_autofree char *parent = g_path_get_dirname(w->path);
    if (g_mkdir_with_parents(parent, 0700) < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot create wallpaper settings: %s", g_strerror(errno));
        return FALSE;
    }
    gsize size;
    g_autofree char *text = g_key_file_to_data(w->config, &size, error);
    return text && g_file_set_contents_full(w->path, text, size,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
}

static void load(Wallpaper *w)
{
    struct stat st;
    if(!g_lstat(w->path,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_size<=1024*1024)
        g_key_file_load_from_file(w->config, w->path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    if (g_key_file_has_key(w->config, "Wallpaper", "source", NULL)) return;
    /* Read an existing wallpaper choice once; never write PCManager's file. */
    g_autofree char *old = g_build_filename(g_get_user_config_dir(), "pcmanager", "LXDE", "pcmanager.conf", NULL);
    g_autoptr(GKeyFile) previous = g_key_file_new();
    if(!g_lstat(old,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_size<=1024*1024)
        g_key_file_load_from_file(previous, old, 0, NULL);
    g_autofree char *image = g_key_file_get_string(previous, "desktop", "wallpaper", NULL);
    if (image && *image) {
        g_key_file_set_string(w->config,"Wallpaper","source",image);
    } else {
        g_autofree char *path=default_wallpaper_path(LUXE_WALLPAPER_DIR,g_get_user_config_dir());
        g_key_file_set_string(w->config,"Wallpaper","source",path);
    }
}

static gboolean image_name(const char *name)
{
    g_autofree char *lower = g_ascii_strdown(name, -1);
    const char *exts[] = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", NULL};
    for (int i=0; exts[i]; i++) if (g_str_has_suffix(lower, exts[i])) return TRUE;
    return FALSE;
}
static gint compare_paths(gconstpointer a, gconstpointer b)
{ return g_strcmp0(*(char *const *)a, *(char *const *)b); }
#define WALLPAPER_SCAN_LIMIT 4096
static GPtrArray *images(const char *source)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    g_autoptr(GDir) dir = g_dir_open(source, 0, NULL);
    if (!dir) {
        if (g_file_test(source, G_FILE_TEST_IS_REGULAR)) g_ptr_array_add(files, g_strdup(source));
        return files;
    }
    const char *name;
    guint visited = 0;
    while (visited < WALLPAPER_SCAN_LIMIT && (name = g_dir_read_name(dir))) {
        visited++;
        if (!image_name(name)) continue;
        char *path = g_build_filename(source, name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) g_ptr_array_add(files, path);
        else g_free(path);
    }
    if (visited == WALLPAPER_SCAN_LIMIT && g_dir_read_name(dir))
        g_warning("Wallpaper directory exceeds %u entries; only the first entries in filesystem order are considered", WALLPAPER_SCAN_LIMIT);
    g_ptr_array_sort(files, compare_paths);
    return files;
}

/* Bundled orbit images opt into their matching collection variants. Personal
 * files and directories retain their existing explicit selection semantics. */
static GPtrArray *theme_images(const char *source, const char *root, const char *config_dir)
{
    g_autofree char *directory=g_path_get_dirname(source);
    if (g_strcmp0(directory,root)!=0 || !g_str_has_suffix(source,".png")) return NULL;
    g_autofree char *source_name=g_path_get_basename(source);
    source_name[strlen(source_name)-4]='\0';
    GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
    const char *collections[] = {"horizon", "river", "veil"};
    g_autofree char *id=appearance_theme(config_dir);
    gboolean source_is_palette=FALSE;
    for(guint i=0;i<G_N_ELEMENTS(collections);i++) {
        g_autofree char *name=g_strdup_printf("luxe-%s-%s.png",collections[i],source_name);
        g_autofree char *path=g_build_filename(root,collections[i],name,NULL);
        if(g_file_test(path,G_FILE_TEST_IS_REGULAR)) source_is_palette=TRUE;
    }
    if(!source_is_palette) {
        g_ptr_array_unref(files);
        return NULL;
    }
    gboolean selected_has_assets=FALSE;
    if(!palette_id_is_safe(id)) g_clear_pointer(&id,g_free);
    for(guint i=0;i<G_N_ELEMENTS(collections);i++) {
        g_autofree char *name=g_strdup_printf("luxe-%s-%s.png",collections[i],id?id:"");
        g_autofree char *path=g_build_filename(root,collections[i],name,NULL);
        if(g_file_test(path,G_FILE_TEST_IS_REGULAR)) selected_has_assets=TRUE;
    }
    if(!selected_has_assets) {
        g_free(g_steal_pointer(&id));
        id=g_strdup(source_name);
    }
    g_autofree char *base=g_strdup_printf("%s.png",id);
    char *path=g_build_filename(root,base,NULL);
    if(g_file_test(path,G_FILE_TEST_IS_REGULAR)) g_ptr_array_add(files,path); else g_free(path);
    for (guint i = 0; i < G_N_ELEMENTS(collections); i++) {
        g_autofree char *name = g_strdup_printf("luxe-%s-%s.png", collections[i], id);
        path = g_build_filename(root, collections[i], name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) g_ptr_array_add(files, path); else g_free(path);
    }
    if(files->len<2) { g_ptr_array_unref(files); return NULL; }
    return files;
}

static GPtrArray *wallpaper_images(const char *source, gboolean *themed)
{
    GPtrArray *files = theme_images(source, LUXE_WALLPAPER_DIR, g_get_user_config_dir());
    if (themed) *themed=files!=NULL;
    return files ? files : images(source);
}

static guint wallpaper_start(GPtrArray *files, const char *current, gboolean rotate,
                             gboolean themed)
{
    for (guint i=0;i<files->len;i++)
        if (g_strcmp0(current,files->pdata[i])==0)
            return rotate ? (i+1)%files->len : i;
    if (themed && rotate && current) {
        /* The palette may have changed since the last login. Keep the
         * collection position while advancing within the new palette. */
        g_autofree char *current_dir=g_path_get_dirname(current);
        for (guint i=0;i<files->len;i++) {
            g_autofree char *candidate_dir=g_path_get_dirname(files->pdata[i]);
            if (g_strcmp0(current_dir,candidate_dir)==0)
                return (i+1)%files->len;
        }
    }
    return 0;
}

static GdkPixbuf *decode_image(const char *path, int max_edge,
                               GCancellable *cancel, GError **error)
{
    struct stat st;
    if (g_stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size > FRAME_MAX_INPUT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Wallpaper is not a bounded regular image file");
        return NULL;
    }
    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autoptr(GFileInputStream) stream = g_file_read(file, cancel, error);
    if (!stream) return NULL;
    return frame_decode_stream(G_INPUT_STREAM(stream), max_edge, NULL, NULL, cancel, error);
}

static gboolean png_write(const gchar *bytes, gsize size, GError **error, gpointer data)
{
    int fd=GPOINTER_TO_INT(data);
    while(size) {
        ssize_t count=write(fd,bytes,size);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) {
            g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(count<0?errno:EIO),
                        "Cannot write wallpaper PNG: %s",g_strerror(count<0?errno:EIO));
            return FALSE;
        }
        bytes+=count; size-=count;
    }
    return TRUE;
}

/* One disposable derivative, never a new selection or a modified original.
 * An unwritable cache must not stop wallpaper painting. */
static gboolean cache_png(GdkPixbuf *pixbuf, const char *source,
                          GCancellable *cancel, GError **error)
{
    g_autofree char *folder=g_build_filename(g_get_user_cache_dir(),"frame",NULL);
    g_autofree char *path=g_build_filename(folder,"wallpaper.png",NULL);
    struct stat st,original;
    if(g_mkdir_with_parents(folder,0700)<0 || g_lstat(folder,&st)<0 ||
       !S_ISDIR(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&0077)) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Wallpaper cache is not a private owned directory");
        return FALSE;
    }
    if(!g_lstat(path,&st)) {
        if(!S_ISREG(st.st_mode) || st.st_uid!=getuid()) {
            g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Wallpaper cache is not an owned regular file");
            return FALSE;
        }
        if(!g_stat(source,&original) && st.st_dev==original.st_dev && st.st_ino==original.st_ino)
            return TRUE; /* Even a selected cache file counts as an original. */
    } else if(errno!=ENOENT) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot inspect wallpaper cache");
        return FALSE;
    }
    g_autofree char *temporary=g_build_filename(folder,".wallpaper-XXXXXX",NULL);
    int fd=g_mkstemp_full(temporary,O_RDWR|O_CLOEXEC,0600);
    if(fd<0) {
        g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(errno),"Cannot create wallpaper PNG: %s",g_strerror(errno));
        return FALSE;
    }
    gboolean ok=gdk_pixbuf_save_to_callback(pixbuf,png_write,GINT_TO_POINTER(fd),"png",error,"compression","7",NULL);
    if(close(fd)<0 && ok) {
        g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(errno),"Cannot close wallpaper PNG: %s",g_strerror(errno));
        ok=FALSE;
    }
    if(ok && cancel && g_cancellable_set_error_if_cancelled(cancel,error)) ok=FALSE;
    if(ok && g_rename(temporary,path)<0) {
        g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(errno),"Cannot replace wallpaper PNG: %s",g_strerror(errno));
        ok=FALSE;
    }
    if(!ok) g_unlink(temporary);
    return ok;
}

static gboolean ensure_display(Wallpaper *w, GError **error)
{
    if (!w->display) {
        w->display=XOpenDisplay(NULL);
        if(w->display) {
            /* Subscribe once, before geometry/painting. Re-selecting after
             * startup display restore can regenerate an initial notification
             * on every repaint, feeding a permanent repaint loop. */
            Window root=DefaultRootWindow(w->display);
            XSelectInput(w->display,root,StructureNotifyMask);
            int randr_error;
            if(XRRQueryExtension(w->display,&w->randr_event,&randr_error))
                XRRSelectInput(w->display,root,RRScreenChangeNotifyMask);
        }
    }
    if (!w->display) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot open wallpaper display");
        return FALSE;
    }
    return TRUE;
}

static gboolean screen_size(Wallpaper *w, int *width, int *height, GError **error)
{
    if (!ensure_display(w,error)) return FALSE;
    Window root=DefaultRootWindow(w->display);
    XWindowAttributes attr;
    XGetWindowAttributes(w->display,root,&attr);
    if (attr.width < 1 || attr.height < 1 || (guint64)attr.width*attr.height > 64000000) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Unsupported wallpaper screen dimensions");
        return FALSE;
    }
    *width=attr.width;
    *height=attr.height;
    return TRUE;
}

static GdkPixbuf *prepare_pixbuf(const char *path, int width, int height,
                                 GCancellable *cancel, GError **error)
{
    g_autoptr(GdkPixbuf) pixbuf=decode_image(path,MIN(4096,MAX(width,height)),cancel,error);
    if(!pixbuf) return NULL;
    if(gdk_pixbuf_get_width(pixbuf)!=width || gdk_pixbuf_get_height(pixbuf)!=height) {
        GdkPixbuf *scaled=gdk_pixbuf_scale_simple(pixbuf,width,height,GDK_INTERP_BILINEAR);
        if(!scaled) {
            g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot resize wallpaper");
            return NULL;
        }
        return scaled;
    }
    return g_steal_pointer(&pixbuf);
}

static cairo_surface_t *prepare_surface(GdkPixbuf *pixbuf, GCancellable *cancel, GError **error)
{
    int width=gdk_pixbuf_get_width(pixbuf),height=gdk_pixbuf_get_height(pixbuf);
    cairo_surface_t *image=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);
    if (cairo_surface_status(image)!=CAIRO_STATUS_SUCCESS) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot allocate wallpaper pixels");
        cairo_surface_destroy(image);
        return NULL;
    }
    guint8 *out=cairo_image_surface_get_data(image),*in=gdk_pixbuf_get_pixels(pixbuf);
    int stride=cairo_image_surface_get_stride(image),channels=gdk_pixbuf_get_n_channels(pixbuf),row=gdk_pixbuf_get_rowstride(pixbuf);
    for (int y=0;y<height;y++) {
      if (cancel && g_cancellable_set_error_if_cancelled(cancel,error)) {
        cairo_surface_destroy(image);
        return NULL;
      }
      for(int x=0;x<width;x++) {
        guint8 *p=in+y*row+x*channels; guint alpha=channels==4?p[3]:255;
        ((guint32 *)(out+y*stride))[x]=(alpha<<24)|((p[0]*alpha/255)<<16)|((p[1]*alpha/255)<<8)|(p[2]*alpha/255);
      }
    }
    cairo_surface_mark_dirty(image);
    return image;
}

static gboolean publish_surface(Wallpaper *w, const char *path,
                                cairo_surface_t *image,
                                Pixmap *previous_pixmap,
                                int *previous_width, int *previous_height,
                                GError **error)
{
    if (!ensure_display(w,error)) return FALSE;
    Window root=DefaultRootWindow(w->display);
    XWindowAttributes attr;
    XGetWindowAttributes(w->display,root,&attr);
    int width=cairo_image_surface_get_width(image),height=cairo_image_surface_get_height(image);
    if(attr.width!=width || attr.height!=height) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Wallpaper screen changed during preparation");
        return FALSE;
    }
    /* The monitor owns the final pixmap. Cairo owns only this temporary
     * rendering connection, so closing it releases its upload/cache resources
     * without losing the background or the RandR event connection. */
    Display *render=XOpenDisplay(DisplayString(w->display));
    if(!render) {
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot open wallpaper rendering connection");
        return FALSE;
    }
    Pixmap next=XCreatePixmap(w->display,root,attr.width,attr.height,attr.depth);
    XSync(w->display,False); /* Publish the pixmap before another client draws. */
    cairo_surface_t *target=cairo_xlib_surface_create(render,next,
        DefaultVisual(render,DefaultScreen(render)),attr.width,attr.height);
    cairo_t *cr=cairo_create(target);
    cairo_set_source_rgb(cr,.08,.08,.1); cairo_paint(cr);
    cairo_set_source_surface(cr,image,0,0); cairo_pattern_set_filter(cairo_get_source(cr),CAIRO_FILTER_BILINEAR); cairo_pattern_set_extend(cairo_get_source(cr),CAIRO_EXTEND_PAD); cairo_paint(cr);
    gboolean ok=cairo_status(cr)==CAIRO_STATUS_SUCCESS && cairo_surface_status(target)==CAIRO_STATUS_SUCCESS;
    cairo_destroy(cr); cairo_surface_destroy(target);
    XSync(render,False); /* Finish pixels before the monitor publishes them. */
    XCloseDisplay(render);
    if (!ok) { XFreePixmap(w->display,next); return FALSE; }
    XSetWindowBackgroundPixmap(w->display,root,next);
    const char *properties[]={"_XROOTPMAP_ID","ESETROOT_PMAP_ID"};
    for(guint i=0;i<G_N_ELEMENTS(properties);i++) XChangeProperty(w->display,root,XInternAtom(w->display,properties[i],False),XA_PIXMAP,32,PropModeReplace,(unsigned char *)&next,1);
    XClearWindow(w->display,root); XFlush(w->display);
    if (previous_pixmap) {
        *previous_pixmap=w->pixmap;
        if (previous_width) *previous_width=w->width;
        if (previous_height) *previous_height=w->height;
    } else if(w->pixmap) XFreePixmap(w->display,w->pixmap);
    w->pixmap=next;
    w->width=attr.width; w->height=attr.height;
    g_key_file_set_string(w->config,"Wallpaper","current",path);
    return TRUE;
}

static void rollback_published(Wallpaper *w, Pixmap previous, int width, int height,
                               const char *previous_path)
{
    Window root=DefaultRootWindow(w->display);
    Pixmap failed=w->pixmap;
    if (previous) {
        XSetWindowBackgroundPixmap(w->display,root,previous);
        const char *properties[]={"_XROOTPMAP_ID","ESETROOT_PMAP_ID"};
        for(guint i=0;i<G_N_ELEMENTS(properties);i++)
            XChangeProperty(w->display,root,XInternAtom(w->display,properties[i],False),
                            XA_PIXMAP,32,PropModeReplace,(unsigned char *)&previous,1);
    } else {
        XSetWindowBackgroundPixmap(w->display,root,None);
        XDeleteProperty(w->display,root,XInternAtom(w->display,"_XROOTPMAP_ID",False));
        XDeleteProperty(w->display,root,XInternAtom(w->display,"ESETROOT_PMAP_ID",False));
    }
    XClearWindow(w->display,root);
    XFlush(w->display);
    w->pixmap=previous;
    w->width=width;
    w->height=height;
    if(failed && failed!=previous) XFreePixmap(w->display,failed);
    if(previous_path) g_key_file_set_string(w->config,"Wallpaper","current",previous_path);
    else g_key_file_remove_key(w->config,"Wallpaper","current",NULL);
}

static gboolean paint_image(Wallpaper *w, const char *path, GError **error)
{
    int width,height;
    if (!screen_size(w,&width,&height,error)) return FALSE;
    g_autoptr(GdkPixbuf) pixbuf=prepare_pixbuf(path,width,height,NULL,error);
    if (!pixbuf) return FALSE;
    cairo_surface_t *image=prepare_surface(pixbuf,NULL,error);
    if (!image) return FALSE;
    gboolean ok=publish_surface(w,path,image,NULL,NULL,NULL,error);
    cairo_surface_destroy(image);
    if (ok) {
        g_autoptr(GError) cache_error=NULL;
        if(!cache_png(pixbuf,path,NULL,&cache_error)) g_warning("Wallpaper: %s",cache_error->message);
    }
    return ok;
}

static gboolean paint(Wallpaper *w, const char *path, GError **error)
{
    gboolean ok = paint_image(w, path, error);
#ifdef __GLIBC__
    /* The processing scope has released every decode/resize/PNG buffer.
     * Return freed pages at this infrequent operation boundary, not on an
     * idle timer. Live X11 pixmaps and display-change ownership stay intact. */
    malloc_trim(0);
#endif
    return ok;
}

/* TRUE means the image is displayed; save_error distinguishes persistence. */
static gboolean apply(Wallpaper *w, gboolean rotate, GError **save_error, guint *file_count)
{
    g_autofree char *source=g_key_file_get_string(w->config,"Wallpaper","source",NULL);
    g_autofree char *current=g_key_file_get_string(w->config,"Wallpaper","current",NULL);
    gboolean themed=FALSE;
    g_autoptr(GPtrArray) files=wallpaper_images(source,&themed);
    if(file_count) *file_count=files->len;
    guint start=wallpaper_start(files,current,rotate,themed);
    for(guint i=0;i<files->len;i++) {
        g_autoptr(GError) error=NULL;
        if(paint(w,files->pdata[(start+i)%files->len],&error)) {save(w,save_error); return TRUE;}
        g_warning("Wallpaper: %s",error?error->message:"painting failed");
    }
    return FALSE;
}

typedef struct {
    GMainLoop *loop;
    GApplicationCommandLine *line;
    int exit_status;
} CommandWait;

typedef struct {
    Wallpaper *wallpaper;
    guint64 generation;
    char *source;
    char *current;
    char *old_source;
    gboolean rotate;
    int width, height;
    CommandWait *wait;
} WallpaperRequest;

typedef struct {
    GPtrArray *files;
    char *path;
    cairo_surface_t *surface;
    GError *cache_error;
} PreparedWallpaper;

static void schedule(Wallpaper *w, guint file_count);

static void prepared_wallpaper_free(PreparedWallpaper *prepared)
{
    if (!prepared) return;
    g_clear_pointer(&prepared->files,g_ptr_array_unref);
    g_free(prepared->path);
    if (prepared->surface) cairo_surface_destroy(prepared->surface);
    g_clear_error(&prepared->cache_error);
    g_free(prepared);
}

static void wallpaper_request_free(WallpaperRequest *request)
{
    g_free(request->source);
    g_free(request->current);
    g_free(request->old_source);
    g_free(request);
}

static void prepare_wallpaper_task(GTask *task, gpointer source,
                                   gpointer task_data, GCancellable *cancel)
{
    (void)source;
    WallpaperRequest *request=task_data;
    g_mutex_lock(&wallpaper_prepare_mutex);
    if (g_cancellable_set_error_if_cancelled(cancel,NULL)) {
        g_mutex_unlock(&wallpaper_prepare_mutex);
        g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Wallpaper preparation was cancelled");
        return;
    }

    PreparedWallpaper *prepared=g_new0(PreparedWallpaper,1);
    gboolean themed=FALSE;
    prepared->files=wallpaper_images(request->source,&themed);
    if (prepared->files->len==0) {
        g_mutex_unlock(&wallpaper_prepare_mutex);
        prepared_wallpaper_free(prepared);
        g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,
                                "Wallpaper source contains no usable image files");
        return;
    }
    guint start=wallpaper_start(prepared->files,request->current,request->rotate,themed);

    GError *last_error=NULL;
    guint attempts=MIN(prepared->files->len,32u);
    for (guint i=0;i<attempts;i++) {
        if (g_cancellable_set_error_if_cancelled(cancel,&last_error)) break;
        const char *path=prepared->files->pdata[(start+i)%prepared->files->len];
        g_autoptr(GError) error=NULL;
        g_autoptr(GdkPixbuf) pixbuf=prepare_pixbuf(path,request->width,request->height,cancel,&error);
        if (!pixbuf) {
            g_clear_error(&last_error);
            last_error=g_steal_pointer(&error);
            continue;
        }
        if (!cache_png(pixbuf,path,cancel,&error)) {
            if (g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) {
                g_clear_error(&last_error);
                last_error=g_steal_pointer(&error);
                break;
            }
            prepared->cache_error=g_steal_pointer(&error);
        }
        prepared->surface=prepare_surface(pixbuf,cancel,&error);
        if (!prepared->surface) {
            if (g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) {
                g_clear_error(&last_error);
                last_error=g_steal_pointer(&error);
                break;
            }
            g_clear_error(&prepared->cache_error);
            g_clear_error(&last_error);
            last_error=g_steal_pointer(&error);
            continue;
        }
        prepared->path=g_strdup(path);
        g_clear_error(&last_error);
        g_mutex_unlock(&wallpaper_prepare_mutex);
        g_task_return_pointer(task,prepared,(GDestroyNotify)prepared_wallpaper_free);
        return;
    }
    guint candidate_count=prepared->files->len;
    g_mutex_unlock(&wallpaper_prepare_mutex);
    prepared_wallpaper_free(prepared);
    if (last_error && g_error_matches(last_error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) {
        g_task_return_error(task,last_error);
    } else {
        g_autofree char *detail=last_error ? g_strdup(last_error->message) : g_strdup("unknown decode error");
        g_clear_error(&last_error);
        g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_FAILED,
            "Could not prepare any of the first %u of %u wallpaper candidates: %s",
            attempts,candidate_count,detail);
    }
}

static gboolean request_wallpaper(Wallpaper *w, gboolean rotate,
                                  const char *old_source, CommandWait *wait);

static void wallpaper_prepared(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    WallpaperRequest *request=data;
    Wallpaper *w=request->wallpaper;
    GError *error=NULL;
    PreparedWallpaper *prepared=g_task_propagate_pointer(G_TASK(result),&error);
    g_assert(w->active_preparations>0);
    w->active_preparations--;
    gboolean current=request->generation==w->request_generation && !w->stopping;
    if (current) g_clear_object(&w->prepare_cancel);

    if (current && prepared) {
        int width=0,height=0;
        if (!screen_size(w,&width,&height,&error)) {
            prepared_wallpaper_free(prepared);
            prepared=NULL;
        } else if (width!=request->width || height!=request->height) {
            prepared_wallpaper_free(prepared);
            prepared=NULL;
            request_wallpaper(w,request->rotate,request->old_source,request->wait);
            wallpaper_request_free(request);
            return;
        } else {
            Pixmap previous=0;
            int previous_width=0,previous_height=0;
            if (publish_surface(w,prepared->path,prepared->surface,
                                &previous,&previous_width,&previous_height,&error)) {
                g_clear_error(&error);
                gboolean saved=save(w,&error);
                if (saved) {
                    if(previous) XFreePixmap(w->display,previous);
                    g_clear_pointer(&w->files,g_ptr_array_unref);
                    w->files=g_ptr_array_ref(prepared->files);
                    if (prepared->cache_error)
                        g_warning("Wallpaper cache: %s",prepared->cache_error->message);
                    schedule(w,w->files->len);
                } else {
                    if (request->old_source)
                        g_key_file_set_string(w->config,"Wallpaper","source",request->old_source);
                    else
                        g_key_file_remove_key(w->config,"Wallpaper","source",NULL);
                    rollback_published(w,previous,previous_width,previous_height,request->current);
                    schedule(w,w->files ? w->files->len : 0);
                }
                prepared_wallpaper_free(prepared);
                if (request->wait) {
                    if (error) {
                        g_application_command_line_printerr(request->wait->line,
                            "Wallpaper was not changed because its settings could not be saved: %s\n",error->message);
                        request->wait->exit_status=1;
                    } else request->wait->exit_status=0;
                } else if (error) {
                    g_warning("Wallpaper selection was not changed because settings could not be saved: %s",error->message);
                }
                g_clear_error(&error);
                if (request->wait) g_main_loop_quit(request->wait->loop);
                wallpaper_request_free(request);
                return;
            }
            prepared_wallpaper_free(prepared);
            prepared=NULL;
        }
    }

    if (current && error) {
        if (request->old_source)
            g_key_file_set_string(w->config,"Wallpaper","source",request->old_source);
        schedule(w,w->files ? w->files->len : 0);
        if (request->wait) {
            g_application_command_line_printerr(request->wait->line,"%s\n",error->message);
            request->wait->exit_status=1;
        } else if (!g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) {
            g_warning("Wallpaper preparation failed: %s",error->message);
        }
        if (!w->display) w->stopping=TRUE;
    } else if (!current && request->wait) {
        g_application_command_line_printerr(request->wait->line,
            "Wallpaper request was superseded by a newer selection\n");
        request->wait->exit_status=1;
    }
    g_clear_error(&error);
    prepared_wallpaper_free(prepared);
    if (request->wait) g_main_loop_quit(request->wait->loop);
    wallpaper_request_free(request);
    if (w->stopping && w->active_preparations==0) g_application_quit(w->app);
}

static gboolean request_wallpaper(Wallpaper *w, gboolean rotate,
                                  const char *old_source, CommandWait *wait)
{
    if (w->stopping) return FALSE;
    g_autofree char *source=g_key_file_get_string(w->config,"Wallpaper","source",NULL);
    g_autofree char *current=g_key_file_get_string(w->config,"Wallpaper","current",NULL);
    int width=0,height=0;
    g_autoptr(GError) error=NULL;
    if (!screen_size(w,&width,&height,&error)) {
        if (wait) {
            g_application_command_line_printerr(wait->line,"%s\n",error->message);
            wait->exit_status=1;
            g_main_loop_quit(wait->loop);
        } else g_warning("Wallpaper preparation failed: %s",error->message);
        return FALSE;
    }
    if (w->prepare_cancel) g_cancellable_cancel(w->prepare_cancel);
    g_clear_object(&w->prepare_cancel);
    w->prepare_cancel=g_cancellable_new();
    WallpaperRequest *request=g_new0(WallpaperRequest,1);
    request->wallpaper=w;
    request->generation=++w->request_generation;
    request->source=g_steal_pointer(&source);
    request->current=g_steal_pointer(&current);
    request->old_source=g_strdup(old_source);
    request->rotate=rotate;
    request->width=width;
    request->height=height;
    request->wait=wait;
    w->active_preparations++;
    GTask *task=g_task_new(w->app,w->prepare_cancel,wallpaper_prepared,request);
    g_task_set_task_data(task,request,NULL);
    g_task_run_in_thread(task,prepare_wallpaper_task);
    g_object_unref(task);
    return TRUE;
}

static gboolean daily(gpointer data);
static void cancel_rotation(Wallpaper *w)
{
    if (!w->rotation) return;
    g_source_remove(w->rotation);
    w->rotation=0;
    if (w->rotation_fd>=0) close(w->rotation_fd);
    w->rotation_fd=-1;
}

#ifdef __linux__
static gboolean rotation_ready(gint fd, GIOCondition condition, gpointer data)
{
    Wallpaper *w=data;
    uint64_t expirations;
    if (!(condition&(G_IO_ERR|G_IO_HUP)) && read(fd,&expirations,sizeof(expirations))<0 && errno==EAGAIN)
        return G_SOURCE_CONTINUE;
    close(fd);
    w->rotation_fd=-1;
    w->rotation=0;
    daily(w);
    return G_SOURCE_REMOVE;
}
#endif

static void schedule(Wallpaper *w, guint file_count)
{
    if (file_count<=1) { cancel_rotation(w); return; }
    /* Repaints and repeated daemon activations must not postpone rotation. */
    if (w->rotation) return;
    w->rotation_fd=-1;
#ifdef __linux__
    /* CLOCK_BOOTTIME includes suspend, unlike GLib's monotonic timeouts. */
    int fd=timerfd_create(CLOCK_BOOTTIME,TFD_CLOEXEC|TFD_NONBLOCK);
    if (fd>=0) {
        struct itimerspec deadline={.it_value={.tv_sec=86400}};
        if (timerfd_settime(fd,0,&deadline,NULL)==0) {
            w->rotation_fd=fd;
            w->rotation=g_unix_fd_add(fd,G_IO_IN|G_IO_ERR|G_IO_HUP,rotation_ready,w);
            return;
        }
        close(fd);
    }
#endif
    w->rotation=g_timeout_add_seconds(86400,daily,w);
}
static gboolean daily(gpointer data)
{
    Wallpaper *w=data;
    w->rotation=0;
    if (w->active_preparations) {
        /* Let the in-flight selection or repaint finish before rotating;
         * otherwise the deadline would cancel its caller's request. */
        w->rotation_fd=-1;
        w->rotation=g_timeout_add(250,daily,w);
        return G_SOURCE_REMOVE;
    }
    if (!w->app) {
        g_autoptr(GError) error=NULL;
        guint file_count=0;
        apply(w,TRUE,&error,&file_count);
        if (error) g_warning("Wallpaper displayed but settings could not be saved: %s",error->message);
        schedule(w,file_count);
        return G_SOURCE_REMOVE;
    }
    if (!request_wallpaper(w,TRUE,NULL,NULL))
        schedule(w,w->files ? w->files->len : 0);
    return G_SOURCE_REMOVE;
}
static gboolean events(gint fd,GIOCondition condition,gpointer data)
{
    (void)fd; Wallpaper *w=data;
    if(condition&(G_IO_HUP|G_IO_ERR)) {g_application_quit(w->app); return G_SOURCE_REMOVE;}
    XEvent event; gboolean resized=FALSE;
    while(XPending(w->display)) {
        XNextEvent(w->display,&event);
        if((event.type==ConfigureNotify && event.xconfigure.window==DefaultRootWindow(w->display))
           || (w->randr_event && event.type==w->randr_event+RRScreenChangeNotify
               && ((XRRScreenChangeNotifyEvent *)&event)->root==DefaultRootWindow(w->display))) {
            XRRUpdateConfiguration(&event);
            resized=TRUE;
        }
    }
    if(resized) {
        XWindowAttributes attr;
        if(XGetWindowAttributes(w->display,DefaultRootWindow(w->display),&attr)
           && (attr.width!=w->width || attr.height!=w->height)) {
            /* A pending preparation rechecks the actual screen size before
             * publishing and retries at that size when necessary. Do not
             * replace a login rotation or selection with a repaint. */
            if (w->app) {
                if (!w->prepare_cancel) request_wallpaper(w,FALSE,NULL,NULL);
            } else {
                g_autoptr(GError) error=NULL;
                apply(w,FALSE,&error,NULL);
                if(error) g_warning("Wallpaper displayed but settings could not be saved: %s",error->message);
            }
        }
    }
    return G_SOURCE_CONTINUE;
}

typedef struct { GSource source; GPollFD poll; Wallpaper *wallpaper; } WallpaperEvents;
static gboolean events_prepare(GSource *source,gint *timeout)
{
    *timeout=-1;
    return XPending(((WallpaperEvents *)source)->wallpaper->display)>0;
}
static gboolean events_check(GSource *source)
{
    WallpaperEvents *watch=(WallpaperEvents *)source;
    return (watch->poll.revents&(G_IO_HUP|G_IO_ERR)) ||
        XPending(watch->wallpaper->display)>0;
}
static gboolean events_dispatch(GSource *source,GSourceFunc callback,gpointer data)
{
    (void)callback; (void)data;
    WallpaperEvents *watch=(WallpaperEvents *)source;
    return events(watch->poll.fd,watch->poll.revents,watch->wallpaper);
}
static void watch_events(Wallpaper *w)
{
    if(w->events) return;
    static GSourceFuncs funcs={events_prepare,events_check,events_dispatch,NULL,NULL,NULL};
    WallpaperEvents *watch=(WallpaperEvents *)g_source_new(&funcs,sizeof(WallpaperEvents));
    watch->wallpaper=w;
    watch->poll.fd=ConnectionNumber(w->display);
    watch->poll.events=G_IO_IN|G_IO_HUP|G_IO_ERR;
    g_source_add_poll((GSource *)watch,&watch->poll);
    w->events=(GSource *)watch;
    g_source_attach(w->events,NULL);
}
static gboolean quit(gpointer data)
{
    Wallpaper *w=data;
    w->stopping=TRUE;
    if (w->prepare_cancel) g_cancellable_cancel(w->prepare_cancel);
    if (w->active_preparations==0) g_application_quit(w->app);
    return G_SOURCE_CONTINUE;
}
static int command(GApplication *app,GApplicationCommandLine *line,gpointer data)
{
    Wallpaper *w=data; int argc; g_auto(GStrv) argv=g_application_command_line_get_arguments(line,&argc);
    /* PCManager may activate the daemon while its login wallpaper is still
     * being prepared. Do not reload its state or cancel that rotation. */
    if (w->held && w->active_preparations>0 && argc==2 &&
        g_str_equal(argv[1],"--wallpaper-daemon")) return 0;
    load(w);
    g_autofree char *old_source=g_key_file_get_string(w->config,"Wallpaper","source",NULL);
    gboolean rotate=!w->held, selected=FALSE;
    for(int i=1;i<argc;i++) {
        if(g_str_equal(argv[i],"--wallpaper-stop")) {
            w->stopping=TRUE;
            if (w->prepare_cancel) g_cancellable_cancel(w->prepare_cancel);
            if (w->active_preparations==0) g_application_quit(app);
            return 0;
        }
        if(g_str_equal(argv[i],"--set-wallpaper")||g_str_equal(argv[i],"--wallpaper-directory")) {
            if (i + 1 >= argc) {
                g_application_command_line_printerr(line,
                    "Wallpaper option %s requires a path\n", argv[i]);
                return 2;
            }
            g_autofree char *cwd=g_application_command_line_get_cwd(line)?g_strdup(g_application_command_line_get_cwd(line)):g_get_current_dir();
            g_autofree char *path=g_canonicalize_filename(argv[++i],cwd);
            if(!g_file_test(path,G_FILE_TEST_EXISTS)) {g_application_command_line_printerr(line,"Wallpaper source does not exist: %s\n",path); return 1;}
            g_key_file_set_string(w->config,"Wallpaper","source",path); rotate=FALSE; selected=TRUE;
        }
    }
    if(!w->held) {g_application_hold(app); w->held=TRUE;}
    if(selected) cancel_rotation(w);
    CommandWait wait={.loop=g_main_loop_new(NULL,FALSE),.line=line,.exit_status=1};
    gboolean started=request_wallpaper(w,rotate,old_source,&wait);
    if (w->display) watch_events(w);
    if (started) g_main_loop_run(wait.loop);
    else if (!w->display) g_application_quit(app);
    g_main_loop_unref(wait.loop);
    return wait.exit_status;
}

typedef struct {
    GtkNativeDialog *dialog;
    GtkApplication *app;
    GSubprocess *child;
} FolderChooser;

static void folder_chooser_finish(FolderChooser *chooser)
{
    gtk_native_dialog_destroy(chooser->dialog);
    g_clear_object(&chooser->dialog);
    g_clear_object(&chooser->child);
    g_application_release(G_APPLICATION(chooser->app));
    g_application_quit(G_APPLICATION(chooser->app));
    g_object_unref(chooser->app);
    g_free(chooser);
}

static void folder_command_finished(GObject *source,GAsyncResult *result,gpointer data)
{
    FolderChooser *chooser=data;
    g_autofree char *stdout_text=NULL,*stderr_text=NULL;
    g_autoptr(GError) error=NULL;
    gboolean communicated=g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source),result,&stdout_text,&stderr_text,&error);
    gboolean successful=communicated && g_subprocess_get_successful(chooser->child);
    if (successful) {
        folder_chooser_finish(chooser);
        return;
    }
    if (communicated && error==NULL) {
        int status=g_subprocess_get_if_exited(chooser->child)
            ? g_subprocess_get_exit_status(chooser->child) : -1;
        g_set_error(&error,G_IO_ERROR,G_IO_ERROR_FAILED,
                    "Wallpaper service exited with status %d%s%s",status,
                    stderr_text && *stderr_text ? ": " : "",
                    stderr_text && *stderr_text ? stderr_text : "");
    }
    luxe_show_error(NULL,"Could not change the wallpaper folder",
                    error ? error->message : "The wallpaper service did not accept the folder.");
    g_clear_object(&chooser->child);
    gtk_native_dialog_show(chooser->dialog);
}

static void folder_response(GtkNativeDialog *dialog,int response,gpointer data)
{
    FolderChooser *chooser=data;
    if (response!=GTK_RESPONSE_ACCEPT) {
        folder_chooser_finish(chooser);
        return;
    }
    g_autoptr(GFile) file=gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
    g_autofree char *path=file ? g_file_get_path(file) : NULL;
    if (!path || !g_file_test(path,G_FILE_TEST_IS_DIR)) {
        luxe_show_error(NULL,"Choose a local wallpaper folder",
                        "The selected location is not a local directory. Choose another folder and try again.");
        gtk_native_dialog_show(dialog);
        return;
    }
    if(response==GTK_RESPONSE_ACCEPT) {
        g_autoptr(GError) error=NULL;
        chooser->child=g_subprocess_new(G_SUBPROCESS_FLAGS_STDERR_PIPE,&error,
                                        "frame","--wallpaper-directory",path,NULL);
        if(!chooser->child) {
            luxe_show_error(NULL,"Could not start the wallpaper service",error->message);
            gtk_native_dialog_show(dialog);
            return;
        }
        gtk_native_dialog_hide(dialog);
        g_subprocess_communicate_utf8_async(chooser->child,NULL,NULL,
                                            folder_command_finished,chooser);
    }
}
static void preferences(GtkApplication *app,gpointer data)
{
    (void)data;
    FolderChooser *chooser=g_new0(FolderChooser,1);
    chooser->app=g_object_ref(app);
    chooser->dialog=GTK_NATIVE_DIALOG(gtk_file_chooser_native_new("Wallpaper folder — rotates daily or at next login",NULL,GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,"Use Folder","Cancel"));
    g_application_hold(G_APPLICATION(app));
    g_signal_connect(chooser->dialog,"response",G_CALLBACK(folder_response),chooser);
    gtk_native_dialog_show(chooser->dialog);
}
int frame_wallpaper_main(int argc,char **argv)
{
    gboolean selected=FALSE,prefs=FALSE;
    for(int i=1;i<argc;i++) {
        if (g_str_equal(argv[i],"--set-wallpaper") || g_str_equal(argv[i],"--wallpaper-directory")) {
            selected=TRUE;
            if (i+1>=argc) {
                g_printerr("%s requires a path\n",argv[i]);
                return 2;
            }
            i++; /* The path is an operand even when it resembles an option. */
            continue;
        }
        if(g_str_equal(argv[i],"--wallpaper-settings")) prefs=selected=TRUE;
        if(g_str_equal(argv[i],"--wallpaper-daemon")||g_str_equal(argv[i],"--wallpaper-stop")) selected=TRUE;
    }
    if(!selected) return -1;
    if(prefs) {
        g_autoptr(GtkApplication) app=GTK_APPLICATION(luxe_application_new("org.luxe.Frame.WallpaperSettings",G_APPLICATION_DEFAULT_FLAGS));
        g_signal_connect(app,"activate",G_CALLBACK(preferences),NULL);
        char *args[]={argv[0],NULL}; return g_application_run(G_APPLICATION(app),1,args);
    }
    Wallpaper w={.config=g_key_file_new(),.path=config_path(),.rotation_fd=-1}; load(&w);
    w.app=g_application_new("org.luxe.Frame.Wallpaper",G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(w.app,"command-line",G_CALLBACK(command),&w);
    guint term=g_unix_signal_add(SIGTERM,quit,&w),intr=g_unix_signal_add(SIGINT,quit,&w);
    int status=g_application_run(w.app,argc,argv);
    g_source_remove(term); g_source_remove(intr);
    cancel_rotation(&w);
    if(w.events) {g_source_destroy(w.events);g_source_unref(w.events);}
    if(w.display) {if(w.pixmap) XFreePixmap(w.display,w.pixmap); XCloseDisplay(w.display);}
    g_clear_object(&w.prepare_cancel);
    g_clear_pointer(&w.files,g_ptr_array_unref);
    g_object_unref(w.app); g_key_file_unref(w.config); g_free(w.path); return status;
}
