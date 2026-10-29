/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#include <glib.h>
static guint directory_reads;
static gboolean count_directory_reads;
static gboolean count_main_loop_ticks(gpointer data)
{
    (*(guint *)data)++;
    return G_SOURCE_CONTINUE;
}
static const char *counted_dir_read_name(GDir *dir)
{
    if (count_directory_reads) directory_reads++;
    return g_dir_read_name(dir);
}
#define g_dir_read_name counted_dir_read_name
#include "../frame-wallpaper.c"
#undef g_dir_read_name
#include <X11/Xutil.h>
#include <webp/encode.h>
#include <poll.h>

static void check_cache(int width,int height,guint32 color)
{
    g_autofree char *path=g_build_filename(g_get_user_cache_dir(),"frame","wallpaper.png",NULL);
    g_autoptr(GdkPixbuf) cached=gdk_pixbuf_new_from_file(path,NULL);
    g_assert_nonnull(cached);
    g_assert_cmpint(gdk_pixbuf_get_width(cached),==,width);
    g_assert_cmpint(gdk_pixbuf_get_height(cached),==,height);
    const guint8 *p=gdk_pixbuf_read_pixels(cached);
    g_assert_cmpuint(p[0],==,color>>24);g_assert_cmpuint(p[1],==,(color>>16)&255);
    g_assert_cmpuint(p[2],==,(color>>8)&255);
    if(gdk_pixbuf_get_has_alpha(cached)) g_assert_cmpuint(p[3],==,color&255);
    struct stat st;g_assert_cmpint(g_stat(path,&st),==,0);
    g_assert_cmpint(st.st_mode&0777,==,0600);
    g_autofree char *folder=g_path_get_dirname(path);
    g_autoptr(GDir) dir=g_dir_open(folder,0,NULL);
    guint files=0;const char *name;
    while((name=g_dir_read_name(dir))) {g_assert_cmpstr(name,==,"wallpaper.png");files++;}
    g_assert_cmpuint(files,==,1);
}

static void check_pixel(Wallpaper *w,guint32 color)
{
    XWindowAttributes attr;XGetWindowAttributes(w->display,DefaultRootWindow(w->display),&attr);
    XImage *pixels=XGetImage(w->display,w->pixmap,0,0,attr.width,attr.height,AllPlanes,ZPixmap);
    g_assert_nonnull(pixels);
    g_assert_cmpuint(XGetPixel(pixels,0,0)&0xffffff,==,color);
    g_assert_cmpuint(XGetPixel(pixels,attr.width-1,attr.height-1)&0xffffff,==,color);
    XDestroyImage(pixels);
}

int main(int argc,char **argv)
{
    g_test_init(&argc,&argv,NULL);
    /* Non-images must count toward the same limit as candidate images. */
    g_autofree char *many=g_dir_make_tmp("frame-scan-XXXXXX",NULL);
    for (guint i=0;i<4100;i++) {
        g_autofree char *name=g_strdup_printf("%04u.txt",i);
        g_autofree char *path=g_build_filename(many,name,NULL);
        g_assert_true(g_file_set_contents(path,"",0,NULL));
    }
    count_directory_reads=TRUE;
    g_test_expect_message(NULL,G_LOG_LEVEL_WARNING,"*Wallpaper directory exceeds 4096 entries*");
    g_autoptr(GPtrArray) limited=images(many);
    g_test_assert_expected_messages();
    count_directory_reads=FALSE;
    g_assert_cmpuint(directory_reads,==,4097); /* 4096 processed plus one truncation probe. */
    g_assert_cmpuint(limited->len,==,0);
    for (guint i=0;i<4100;i++) {
        g_autofree char *name=g_strdup_printf("%04u.txt",i);
        g_autofree char *path=g_build_filename(many,name,NULL);
        g_assert_cmpint(g_unlink(path),==,0);
    }
    g_assert_cmpint(g_rmdir(many),==,0);
    g_autofree char *folder=g_dir_make_tmp("frame-rotation-XXXXXX",NULL);
    /* A bundled source follows the active palette across collections; a
     * personal file does not become a theme selection. */
    g_autofree char *theme_root=g_build_filename(folder,"backgrounds",NULL);
    g_autofree char *config_dir=g_build_filename(folder,"config",NULL);
    g_autofree char *appearance_dir=g_build_filename(config_dir,"luxe",NULL);
    g_assert_cmpint(g_mkdir_with_parents(appearance_dir,0700),==,0);
    const char *dirs[]={"", "horizon", "river", "veil"};
    for (guint i=0;i<G_N_ELEMENTS(dirs);i++) {
        g_autofree char *dir=g_build_filename(theme_root,dirs[i],NULL);
        g_assert_cmpint(g_mkdir_with_parents(dir,0700),==,0);
        g_autofree char *name=i ? g_strdup_printf("luxe-%s-dark.png",dirs[i]) : g_strdup("dark.png");
        g_autofree char *path=g_build_filename(dir,name,NULL);
        g_assert_true(g_file_set_contents(path,"image",-1,NULL));
        g_free(g_steal_pointer(&name));
        name=i ? g_strdup_printf("luxe-%s-mocha.png",dirs[i]) : g_strdup("mocha.png");
        path=g_build_filename(dir,name,NULL);
        g_assert_true(g_file_set_contents(path,"image",-1,NULL));
    }
    g_autofree char *fallback=g_build_filename(theme_root,"tokyo-night.png",NULL);
    g_assert_true(g_file_set_contents(fallback,"image",-1,NULL));
    g_autofree char *appearance=g_build_filename(appearance_dir,"appearance.ini",NULL);
    g_assert_true(g_file_set_contents(appearance,"[Appearance]\ntheme=dark\n",-1,NULL));
    g_autofree char *bundled=g_build_filename(theme_root,"dark.png",NULL);
    g_autoptr(GPtrArray) themed=theme_images(bundled,theme_root,config_dir);
    g_assert_nonnull(themed);g_assert_cmpuint(themed->len,==,4);
    g_assert_true(g_str_has_suffix(themed->pdata[0],"/dark.png"));
    g_assert_true(g_str_has_suffix(themed->pdata[1],"/luxe-horizon-dark.png"));
    g_assert_true(g_str_has_suffix(themed->pdata[2],"/luxe-river-dark.png"));
    g_assert_true(g_str_has_suffix(themed->pdata[3],"/luxe-veil-dark.png"));
    g_autofree char *default_path=default_wallpaper_path(theme_root,config_dir);
    g_assert_true(g_str_has_suffix(default_path,"/dark.png"));
    g_assert_true(g_file_set_contents(appearance,"[Appearance]\ntheme=unavailable\n",-1,NULL));
    g_clear_pointer(&themed,g_ptr_array_unref);
    themed=theme_images(bundled,theme_root,config_dir);
    g_assert_nonnull(themed);g_assert_cmpuint(themed->len,==,4);
    g_assert_true(g_str_has_suffix(themed->pdata[0],"/dark.png"));
    g_autofree char *missing=g_build_filename(theme_root,"river","luxe-river-dark.png",NULL);
    g_assert_cmpint(g_remove(missing),==,0);
    g_clear_pointer(&themed,g_ptr_array_unref);
    themed=theme_images(bundled,theme_root,config_dir);
    g_assert_nonnull(themed);g_assert_cmpuint(themed->len,==,3);
    g_assert_true(g_file_set_contents(missing,"image",-1,NULL));
    g_autofree char *outside=g_build_filename(folder,"outside.png",NULL);
    g_assert_true(g_file_set_contents(outside,"image",-1,NULL));
    g_assert_true(g_file_set_contents(appearance,"[Appearance]\ntheme=../outside\n",-1,NULL));
    g_clear_pointer(&default_path,g_free);
    default_path=default_wallpaper_path(theme_root,config_dir);
    g_assert_true(g_str_has_suffix(default_path,"/tokyo-night.png"));
    g_assert_true(g_file_set_contents(appearance,"[Appearance]\ntheme=dark\n",-1,NULL));
    g_assert_true(g_file_set_contents(appearance,"[Appearance]\ntheme=mocha\n",-1,NULL));
    g_clear_pointer(&themed,g_ptr_array_unref);
    themed=theme_images(bundled,theme_root,config_dir);
    g_assert_nonnull(themed);g_assert_cmpuint(themed->len,==,4);
    g_assert_true(g_str_has_suffix(themed->pdata[0],"/mocha.png"));
    g_assert_true(g_str_has_suffix(themed->pdata[1],"/luxe-horizon-mocha.png"));
    g_autofree char *personal=g_build_filename(folder,"personal.png",NULL);
    g_assert_null(theme_images(personal,theme_root,config_dir));
    for (guint i=0;i<G_N_ELEMENTS(dirs);i++) {
        g_autofree char *dir=g_build_filename(theme_root,dirs[i],NULL);
        g_autofree char *name=i ? g_strdup_printf("luxe-%s-dark.png",dirs[i]) : g_strdup("dark.png");
        g_autofree char *path=g_build_filename(dir,name,NULL);
        g_assert_cmpint(g_remove(path),==,0);
        g_free(g_steal_pointer(&name));
        name=i ? g_strdup_printf("luxe-%s-mocha.png",dirs[i]) : g_strdup("mocha.png");
        g_free(g_steal_pointer(&path));
        path=g_build_filename(dir,name,NULL);
        g_assert_cmpint(g_remove(path),==,0);
        if (i) g_assert_cmpint(g_rmdir(dir),==,0);
    }
    g_assert_cmpint(g_remove(fallback),==,0);
    g_assert_cmpint(g_rmdir(theme_root),==,0);
    g_assert_cmpint(g_remove(appearance),==,0);
    g_assert_cmpint(g_rmdir(appearance_dir),==,0);
    g_assert_cmpint(g_rmdir(config_dir),==,0);
    g_assert_cmpint(g_remove(outside),==,0);
    g_autofree char *a=g_build_filename(folder,"a.png",NULL),*b=g_build_filename(folder,"b.png",NULL);
    g_autoptr(GdkPixbuf) image=gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,2,2);
    gdk_pixbuf_fill(image,0x00ff00ff);g_assert_true(gdk_pixbuf_save(image,a,"png",NULL,NULL));
    gdk_pixbuf_fill(image,0xff0000ff);g_assert_true(gdk_pixbuf_save(image,b,"png",NULL,NULL));
    Wallpaper w={.config=g_key_file_new(),.path=g_build_filename(folder,"settings.ini",NULL)};
    g_key_file_set_string(w.config,"Wallpaper","source",folder);
    g_autoptr(GError) save_error=NULL;
    guint file_count=0;
    directory_reads=0;count_directory_reads=TRUE;
    g_assert_true(apply(&w,FALSE,&save_error,&file_count));g_assert_no_error(save_error);
    guint reads_after_apply=directory_reads;
    g_assert_cmpuint(file_count,==,2);
    schedule(&w,file_count);
    g_assert_cmpuint(directory_reads,==,reads_after_apply); /* Scheduling reuses the scan. */
    count_directory_reads=FALSE;
    g_assert_cmpuint(w.rotation,>,0);
    g_source_remove(w.rotation);w.rotation=0;
    /* A displayed image and a failed settings write are separate outcomes. */
    g_autofree char *blocked=g_build_filename(folder,"blocked",NULL);
    g_assert_true(g_file_set_contents(blocked,"not a directory",-1,NULL));
    char *working_path=w.path;
    w.path=g_build_filename(blocked,"settings.ini",NULL);
    g_assert_true(apply(&w,FALSE,&save_error,&file_count));
    g_assert_nonnull(save_error);
    g_clear_error(&save_error);
    g_free(w.path);w.path=working_path;
    g_assert_cmpint(g_unlink(blocked),==,0);
    schedule(&w,file_count);g_assert_cmpuint(w.rotation,>,0);
    g_source_remove(w.rotation);w.rotation=0;
    daily(&w);
    g_autofree char *current=g_key_file_get_string(w.config,"Wallpaper","current",NULL);
    g_assert_cmpstr(current,==,b);g_assert_cmpuint(w.rotation,>,0);
    XWindowAttributes attr;XGetWindowAttributes(w.display,DefaultRootWindow(w.display),&attr);
    g_assert_true((attr.your_event_mask&StructureNotifyMask)!=0);
    check_pixel(&w,0xff0000);check_cache(attr.width,attr.height,0xff0000ff);

    /* A 4K original produces the desktop-sized PNG without changing its bytes. */
    g_autofree char *large=g_build_filename(folder,"large.png",NULL);
    g_autoptr(GdkPixbuf) full=gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,3840,2160);
    gdk_pixbuf_fill(full,0x405080ff);g_assert_true(gdk_pixbuf_save(full,large,"png",NULL,NULL));
    g_autofree char *before=NULL,*after=NULL;gsize before_size,after_size;
    g_assert_true(g_file_get_contents(large,&before,&before_size,NULL));
    for(int i=0;i<5;i++) g_assert_true(paint(&w,large,NULL));
    check_pixel(&w,0x405080);check_cache(attr.width,attr.height,0x405080ff);
    g_assert_true(g_file_get_contents(large,&after,&after_size,NULL));
    g_assert_cmpmem(before,before_size,after,after_size);
    g_autofree char *selected=g_key_file_get_string(w.config,"Wallpaper","current",NULL);
    g_assert_cmpstr(selected,==,large);

    /* Solid fills cannot expose an original-sized crop. Distinct 4K corners
     * must reach all four screen quadrants after stretching. */
    g_autofree char *landmarks=g_build_filename(folder,"landmarks.png",NULL);
    guint8 *pixels=gdk_pixbuf_get_pixels(full);
    int rowstride=gdk_pixbuf_get_rowstride(full);
    const guint32 colors[]={0xff0000,0x00ff00,0x0000ff,0xffff00};
    for(int y=0;y<2160;y++) for(int x=0;x<3840;x++) {
        guint32 color=colors[(y>=1080)*2+(x>=1920)];
        guint8 *p=pixels+y*rowstride+x*3;
        p[0]=color>>16; p[1]=color>>8; p[2]=color;
    }
    g_assert_true(gdk_pixbuf_save(full,landmarks,"png",NULL,NULL));
    g_assert_true(paint(&w,landmarks,NULL));
    XImage *screen=XGetImage(w.display,w.pixmap,0,0,attr.width,attr.height,AllPlanes,ZPixmap);
    g_assert_nonnull(screen);
    for(guint i=0;i<4;i++)
        g_assert_cmphex(XGetPixel(screen,(i%2?3:1)*attr.width/4,(i/2?3:1)*attr.height/4)&0xffffff,==,colors[i]);
    XDestroyImage(screen);

    /* Native WebP and alpha use the same exact-size lossless PNG path. */
    g_autofree char *webp=g_build_filename(folder,"alpha.webp",NULL);
    g_autoptr(GdkPixbuf) alpha=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,8,4);
    gdk_pixbuf_fill(alpha,0x80402080);
    guint8 *encoded=NULL;size_t length=WebPEncodeLosslessRGBA(gdk_pixbuf_read_pixels(alpha),8,4,gdk_pixbuf_get_rowstride(alpha),&encoded);
    g_assert_cmpuint(length,>,0);g_assert_true(g_file_set_contents(webp,(char *)encoded,length,NULL));WebPFree(encoded);
    g_assert_true(paint(&w,webp,NULL));check_cache(attr.width,attr.height,0x80402080);
    /* Premultiplied source over the existing (.08,.08,.1) background. */
    check_pixel(&w,0x4a2a1c);
    Pixmap previous=w.pixmap;
    g_autofree char *bad=g_build_filename(folder,"bad.png",NULL);
    g_assert_true(g_file_set_contents(bad,"invalid",-1,NULL));
    g_autoptr(GError) error=NULL;g_assert_false(paint(&w,bad,&error));g_assert_nonnull(error);
    g_assert_cmpuint(w.pixmap,==,previous);check_cache(attr.width,attr.height,0x80402080);

    /* Cache problems do not block painting or follow symlinks into originals. */
    g_autofree char *cache=g_build_filename(g_get_user_cache_dir(),"frame","wallpaper.png",NULL);
    g_assert_cmpint(g_remove(cache),==,0);g_assert_cmpint(symlink(large,cache),==,0);
    g_test_expect_message(NULL,G_LOG_LEVEL_WARNING,"*cache is not an owned regular file*");
    g_assert_true(paint(&w,a,NULL));g_test_assert_expected_messages();check_pixel(&w,0x00ff00);
    g_clear_pointer(&after,g_free);g_assert_true(g_file_get_contents(large,&after,&after_size,NULL));
    g_assert_cmpmem(before,before_size,after,after_size);
    g_assert_cmpint(g_remove(cache),==,0);g_assert_true(paint(&w,b,NULL));
    /* Selecting the derivative itself must not overwrite that selected file. */
    struct stat original,retained;g_assert_cmpint(g_stat(cache,&original),==,0);
    g_assert_true(paint(&w,cache,NULL));g_assert_cmpint(g_stat(cache,&retained),==,0);
    g_assert_cmpuint(original.st_ino,==,retained.st_ino);
    g_autofree char *cache_folder=g_path_get_dirname(cache);
    g_assert_cmpint(g_remove(cache),==,0);g_assert_cmpint(g_rmdir(cache_folder),==,0);
    g_assert_true(g_file_set_contents(cache_folder,"unwritable cache directory",-1,NULL));
    g_test_expect_message(NULL,G_LOG_LEVEL_WARNING,"*cache is not a private owned directory*");
    g_assert_true(paint(&w,a,NULL));g_test_assert_expected_messages();check_pixel(&w,0x00ff00);
    g_assert_cmpint(g_remove(cache_folder),==,0);g_assert_true(paint(&w,b,NULL));

    /* The production RandR handler regenerates a removed derivative, without
     * a new selection or advancing directory rotation. */
    g_assert_cmpint(g_remove(cache),==,0);
    /* XSync/geometry requests can move notifications from the socket into
     * Xlib. Its FD is then unreadable, but the GLib source must still wake. */
    while(XPending(w.display)) { XEvent pending; XNextEvent(w.display,&pending); }
    XEvent event={.xconfigure={.type=ConfigureNotify,
        .display=w.display,.window=DefaultRootWindow(w.display),.width=attr.width+1,.height=attr.height}};
    XPutBackEvent(w.display,&event);
    w.width=attr.width+1; /* Previously painted size differs from the real root. */
    struct pollfd fd={.fd=ConnectionNumber(w.display),.events=POLLIN};
    g_assert_cmpint(poll(&fd,1,0),==,0);
    guint legacy=g_unix_fd_add(fd.fd,G_IO_IN,events,&w);
    g_assert_false(g_main_context_iteration(NULL,FALSE));
    g_assert_false(g_file_test(cache,G_FILE_TEST_EXISTS));
    g_source_remove(legacy);
    watch_events(&w);
    g_assert_true(g_main_context_iteration(NULL,FALSE));
    check_cache(attr.width,attr.height,0xff0000ff);
    /* Repeated same-size RandR/configuration events must not redraw a static
     * desktop or rewrite the PNG even when Xlib's screen cache was stale. */
    struct stat stable,unchanged;g_assert_cmpint(g_stat(cache,&stable),==,0);
    int event_base,error_base;
    if(XRRQueryExtension(w.display,&event_base,&error_base)) {
        XRRScreenChangeNotifyEvent change={.type=event_base+RRScreenChangeNotify,
            .display=w.display,.root=DefaultRootWindow(w.display),
            .window=DefaultRootWindow(w.display),.rotation=RR_Rotate_0,
            .width=attr.width,.height=attr.height};
        XEvent notification={0};*(XRRScreenChangeNotifyEvent *)&notification=change;
        for(int i=0;i<20;i++) {
            XPutBackEvent(w.display,&notification);
            g_assert_true(g_main_context_iteration(NULL,FALSE));
            g_assert_cmpint(g_stat(cache,&unchanged),==,0);
            g_assert_cmpuint(stable.st_ino,==,unchanged.st_ino);
        }
    }

    /* Production preparation runs off the main loop. A newer selection
     * cancels/supersedes the slow first request and only the latest paints. */
    w.app=g_application_new("org.luxe.Frame.Wallpaper.Behavior",G_APPLICATION_NON_UNIQUE);
    g_key_file_set_string(w.config,"Wallpaper","source",large);
    guint ticks=0;
    guint tick_source=g_timeout_add(1,count_main_loop_ticks,&ticks);
    g_assert_true(request_wallpaper(&w,FALSE,NULL,NULL));
    g_key_file_set_string(w.config,"Wallpaper","source",b);
    g_assert_true(request_wallpaper(&w,FALSE,NULL,NULL));
    while(w.active_preparations>0) g_main_context_iteration(NULL,TRUE);
    g_source_remove(tick_source);
    g_assert_cmpuint(ticks,>,0);
    g_autofree char *latest=g_key_file_get_string(w.config,"Wallpaper","current",NULL);
    g_assert_cmpstr(latest,==,b);
    check_pixel(&w,0xff0000);

    /* Candidate attempts are capped even when a directory contains many
     * malformed image-looking files. */
    g_autofree char *bad_directory=g_dir_make_tmp("frame-bad-wallpapers-XXXXXX",NULL);
    for(guint i=0;i<40;i++) {
        g_autofree char *name=g_strdup_printf("%02u.png",i);
        g_autofree char *path=g_build_filename(bad_directory,name,NULL);
        g_assert_true(g_file_set_contents(path,"invalid",-1,NULL));
    }
    g_key_file_set_string(w.config,"Wallpaper","source",bad_directory);
    g_test_expect_message(NULL,G_LOG_LEVEL_WARNING,"*Could not prepare any of the first 32 of 40 wallpaper candidates*");
    g_assert_true(request_wallpaper(&w,FALSE,NULL,NULL));
    while(w.active_preparations>0) g_main_context_iteration(NULL,TRUE);
    g_test_assert_expected_messages();
    for(guint i=0;i<40;i++) {
        g_autofree char *name=g_strdup_printf("%02u.png",i);
        g_autofree char *path=g_build_filename(bad_directory,name,NULL);
        g_assert_cmpint(g_unlink(path),==,0);
    }
    g_assert_cmpint(g_rmdir(bad_directory),==,0);

    g_source_destroy(w.events);g_source_unref(w.events);
    if(w.rotation) g_source_remove(w.rotation);
    XFreePixmap(w.display,w.pixmap);XCloseDisplay(w.display);
    g_clear_object(&w.prepare_cancel);g_clear_object(&w.app);g_clear_pointer(&w.files,g_ptr_array_unref);
    g_key_file_unref(w.config);g_remove(a);g_remove(b);g_remove(large);g_remove(landmarks);g_remove(webp);g_remove(bad);g_remove(w.path);g_free(w.path);g_rmdir(folder);
    return 0;
}
