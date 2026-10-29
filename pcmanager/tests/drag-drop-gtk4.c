/* Run only on an isolated X11 display with disposable fixtures/XDG config. */
#include "../src/browser-gtk4.c"
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>

static void pump(void) {
    gint64 end=g_get_monotonic_time()+180000;
    while(g_get_monotonic_time()<end){while(g_main_context_iteration(NULL,FALSE)){}g_usleep(1000);}
}
static void loaded(Tab*t){for(int i=0;i<100;i++){pump();if(!gtk_directory_list_is_loading(t->directory))break;}g_assert_null(gtk_directory_list_get_error(t->directory));}
static void xdo(const char *command) { char *cmd=g_strconcat("xdotool ",command,NULL);GError*e=NULL;int status;g_spawn_command_line_sync(cmd,NULL,NULL,&status,&e);g_assert_no_error(e);g_assert_cmpint(status,==,0);g_free(cmd);pump();}
static GtkWidget *find_row(GtkWidget*w,const char*name){
    if(!gtk_widget_get_mapped(w))return NULL;
    GtkListItem*item=g_object_get_data(G_OBJECT(w),"list-item");
    if(item){GFileInfo*info=gtk_list_item_get_item(item);if(info&&!g_strcmp0(g_file_info_get_name(info),name))return w;}
    for(GtkWidget*c=gtk_widget_get_first_child(w);c;c=gtk_widget_get_next_sibling(c)){GtkWidget*r=find_row(c,name);if(r)return r;}return NULL;
}
static void coordinates(GtkWidget*w,double fx,double fy,int*x,int*y){
    GtkWidget*win=GTK_WIDGET(gtk_widget_get_root(w));double wx,wy;
    g_assert_true(gtk_widget_translate_coordinates(w,win,gtk_widget_get_width(w)*fx,gtk_widget_get_height(w)*fy,&wx,&wy));
    GdkSurface*s=gtk_native_get_surface(GTK_NATIVE(win));Display*d=GDK_DISPLAY_XDISPLAY(gdk_surface_get_display(s));Window child;int rx,ry;
    XTranslateCoordinates(d,gdk_x11_surface_get_xid(s),DefaultRootWindow(d),0,0,&rx,&ry,&child);*x=rx+wx;*y=ry+wy;
}
/* Find a mapped row for `name` whose on-screen position really resolves
 * back to that row: guards against stale allocations while the grid is
 * settling after scroll/model churn. */
static GtkWidget *stable_row(Tab*t,GtkWidget*view,const char*name){
    GtkWidget*root=GTK_WIDGET(gtk_widget_get_root(view));
    for(int i=0;i<80;i++){
        GtkWidget*w=find_row(view,name);
        if(w && !gtk_directory_list_is_loading(t->directory) && !t->refresh_source){
            int x,y;coordinates(w,.4,.5,&x,&y);
            double lx,ly;
            if(gtk_widget_translate_coordinates(root,view,x,y,&lx,&ly)){
                GtkWidget*hit=gtk_widget_pick(view,MAX(lx,0.0),MAX(ly,0.0),GTK_PICK_DEFAULT);
                for(GtkWidget*p=hit;p;p=gtk_widget_get_parent(p)){
                    GtkListItem*it=g_object_get_data(G_OBJECT(p),"list-item");
                    if(it){GFileInfo*inf=gtk_list_item_get_item(it);
                        if(inf&&!strcmp(g_file_info_get_name(inf),name))return w;
                        break;}
                }
            }
        }
        pump();
    }
    return NULL;
}
static void pointer_drag(GtkWidget*from,GtkWidget*to,gboolean background,gboolean copy){
    /* A drag that follows a foreign-window drop needs a fresh motion event
     * before its press or GTK never initiates it: park the pointer over
     * window chrome (fixed test geometry) first, then read coordinates with
     * no churn in between (callers pass stable_row-verified widgets). */
    xdo("mousemove 750 100");pump();
    int x1,y1,x2,y2;coordinates(from,.4,.5,&x1,&y1);coordinates(to,.6,background?.85:.5,&x2,&y2);
    char*cmd=g_strdup_printf("mousemove %d %d",x1,y1);xdo(cmd);g_free(cmd);
    if(copy)xdo("keydown ctrl");
    xdo("mousedown 1");
    for(int step=1;step<=10;step++){cmd=g_strdup_printf("mousemove %d %d",x1+(x2-x1)*step/10,y1+(y2-y1)*step/10);xdo(cmd);g_free(cmd);}
    xdo("mouseup 1");if(copy)xdo("keyup ctrl");
    for(int i=0;i<100;i++){pump();gboolean busy=FALSE;for(GList*l=browsers;l;l=l->next)busy|=((Browser*)l->data)->operation!=NULL;if(!busy)break;}
}
static void exists(const char*root,const char*name,gboolean expected){char*p=g_build_filename(root,name,NULL);g_assert_cmpint(g_file_test(p,G_FILE_TEST_EXISTS),==,expected);g_free(p);}
static void select_name(Tab*t,const char*name,gboolean clear){for(guint i=0;i<g_list_model_get_n_items(G_LIST_MODEL(t->sorted));i++){GFileInfo*info=g_list_model_get_item(G_LIST_MODEL(t->sorted),i);if(!strcmp(g_file_info_get_name(info),name))gtk_selection_model_select_item(GTK_SELECTION_MODEL(t->selection),i,clear);g_object_unref(info);}}
static GtkWidget *permission_check(GtkWidget *widget, guint32 bit) {
    if (GTK_IS_CHECK_BUTTON(widget) && GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "permission-bit")) == bit) return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = permission_check(child, bit); if (found) return found;
    }
    return NULL;
}
static GtkWidget *named_button(GtkWidget *widget, const char *name) {
    if (GTK_IS_BUTTON(widget) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), name)) return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = named_button(child, name); if (found) return found;
    }
    return NULL;
}
static guint32 permission_mode(const char *path) { struct stat info; g_assert_cmpint(stat(path, &info), ==, 0); return info.st_mode & 07777; }
static gboolean is_selected(Tab*t,const char*name){for(guint i=0;i<g_list_model_get_n_items(G_LIST_MODEL(t->sorted));i++){GFileInfo*info=g_list_model_get_item(G_LIST_MODEL(t->sorted),i);gboolean match=info&&!strcmp(g_file_info_get_name(info),name);gboolean sel=match&&gtk_selection_model_is_selected(GTK_SELECTION_MODEL(t->selection),i);if(info)g_object_unref(info);if(match)return sel;}return FALSE;}
static gboolean model_has(Tab*t,const char*name){for(guint i=0;i<g_list_model_get_n_items(G_LIST_MODEL(t->sorted));i++){GFileInfo*info=g_list_model_get_item(G_LIST_MODEL(t->sorted),i);gboolean match=info&&!strcmp(g_file_info_get_name(info),name);if(info)g_object_unref(info);if(match)return TRUE;}return FALSE;}
static gboolean calculating(GtkWidget*w){
    if(GTK_IS_LABEL(w)&&!g_strcmp0(gtk_label_get_text(GTK_LABEL(w)),_("Calculating…")))return TRUE;
    for(GtkWidget*c=gtk_widget_get_first_child(w);c;c=gtk_widget_get_next_sibling(c))if(calculating(c))return TRUE;
    return FALSE;
}
static GtkWindow*property_window(Browser*b){
    GListModel*windows=gtk_window_get_toplevels();
    for(guint i=0;i<g_list_model_get_n_items(windows);i++){
        GtkWindow*w=g_list_model_get_item(windows,i);
        if(gtk_window_get_transient_for(w)==GTK_WINDOW(b))return w;
        g_object_unref(w);
    }
    return NULL;
}
static void place_window(GtkWindow*w,int x,int y){gtk_window_set_default_size(w,690,620);gtk_window_present(w);pump();GdkSurface*s=gtk_native_get_surface(GTK_NATIVE(w));char*cmd=g_strdup_printf("windowmove %lu %d %d",gdk_x11_surface_get_xid(s),x,y);xdo(cmd);g_free(cmd);}

static char *exported_uris;
typedef struct { GdkDrop *drop; GInputStream *stream; GString *text; } Export;
static void export_chunk(GObject*object,GAsyncResult*result,gpointer data){
    Export*e=data;GError*error=NULL;GBytes*bytes=g_input_stream_read_bytes_finish(G_INPUT_STREAM(object),result,&error);g_assert_no_error(error);
    gsize size;const char*buffer=g_bytes_get_data(bytes,&size);
    if(size){g_string_append_len(e->text,buffer,size);g_input_stream_read_bytes_async(e->stream,4096,G_PRIORITY_DEFAULT,NULL,export_chunk,e);}
    else{exported_uris=g_string_free(e->text,FALSE);gdk_drop_finish(e->drop,GDK_ACTION_COPY);g_object_unref(e->stream);g_object_unref(e->drop);g_free(e);}
    g_bytes_unref(bytes);
}
static void export_stream(GObject*object,GAsyncResult*result,gpointer data){
    Export*e=data;GError*error=NULL;const char*mime=NULL;e->stream=gdk_drop_read_finish(GDK_DROP(object),result,&mime,&error);g_assert_no_error(error);g_assert_cmpstr(mime,==,"text/uri-list");
    g_input_stream_read_bytes_async(e->stream,4096,G_PRIORITY_DEFAULT,NULL,export_chunk,e);
}
static gboolean export_drop(GtkDropTargetAsync*target,GdkDrop*drop,double x,double y,gpointer data){
    (void)target;(void)x;(void)y;(void)data;Export*e=g_new0(Export,1);e->drop=g_object_ref(drop);e->text=g_string_new(NULL);const char*mimes[]={"text/uri-list",NULL};gdk_drop_read_async(drop,mimes,G_PRIORITY_DEFAULT,NULL,export_stream,e);return TRUE;
}
/* Step 10b external MOVE destination: copies the dropped URIs into
 * move_target_dir itself, then finishes with MOVE. The source must then
 * delete its files through the Trash. */
static char *move_target_dir;
typedef struct { GdkDrop *drop; GInputStream *stream; GString *text; } MoveImport;
static void move_chunk(GObject*object,GAsyncResult*result,gpointer data){
    MoveImport*m=data;GError*error=NULL;GBytes*bytes=g_input_stream_read_bytes_finish(G_INPUT_STREAM(object),result,&error);g_assert_no_error(error);
    gsize size;const char*buffer=g_bytes_get_data(bytes,&size);
    if(size){g_string_append_len(m->text,buffer,size);g_input_stream_read_bytes_async(m->stream,4096,G_PRIORITY_DEFAULT,NULL,move_chunk,m);}
    else{
        char**lines=g_strsplit(m->text->str,"\r\n",-1);
        for(int i=0;lines[i];i++){
            if(!*lines[i])continue;
            char*path=g_filename_from_uri(lines[i],NULL,NULL);g_assert_nonnull(path);
            char*base=g_path_get_basename(path);char*dest=g_build_filename(move_target_dir,base,NULL);
            GFile*src=g_file_new_for_path(path),*dst=g_file_new_for_path(dest);
            g_assert_true(g_file_copy(src,dst,G_FILE_COPY_NONE,NULL,NULL,NULL,&error));g_assert_no_error(error);
            g_free(path);g_free(base);g_free(dest);g_object_unref(src);g_object_unref(dst);
        }
        g_strfreev(lines);
        g_string_free(m->text,TRUE);
        gdk_drop_finish(m->drop,GDK_ACTION_MOVE);
        g_object_unref(m->stream);g_object_unref(m->drop);g_free(m);
    }
    g_bytes_unref(bytes);
}
static void move_stream(GObject*object,GAsyncResult*result,gpointer data){
    MoveImport*m=data;GError*error=NULL;const char*mime=NULL;m->stream=gdk_drop_read_finish(GDK_DROP(object),result,&mime,&error);g_assert_no_error(error);g_assert_cmpstr(mime,==,"text/uri-list");
    g_input_stream_read_bytes_async(m->stream,4096,G_PRIORITY_DEFAULT,NULL,move_chunk,m);
}
static gboolean move_motion(GtkDropTargetAsync*t,GdkDrop*d,double x,double y,gpointer data){(void)t;(void)d;(void)x;(void)y;(void)data;return GDK_ACTION_MOVE;}
static gboolean move_drop(GtkDropTargetAsync*target,GdkDrop*drop,double x,double y,gpointer data){
    (void)target;(void)x;(void)y;(void)data;MoveImport*m=g_new0(MoveImport,1);m->drop=g_object_ref(drop);m->text=g_string_new(NULL);const char*mimes[]={"text/uri-list",NULL};gdk_drop_read_async(drop,mimes,G_PRIORITY_DEFAULT,NULL,move_stream,m);return TRUE;
}
static GdkDragAction export_motion(GtkDropTargetAsync*t,GdkDrop*d,double x,double y,gpointer data){(void)t;(void)d;(void)x;(void)y;(void)data;return GDK_ACTION_COPY;}
int main(int argc,char**argv){
    g_assert_cmpint(argc,==,3);g_set_prgname("pcmanager-dnd-test");gtk_init();luxe_ui_init();fm_init(NULL);pcmgr_browser_setup("drag-test",NULL);
    GFile*left=g_file_new_for_path(argv[1]),*right=g_file_new_for_path(argv[2]);
    Browser*a=new_browser();Tab*ta=add_tab(a,left);g_action_group_activate_action(G_ACTION_GROUP(a->actions),"list",NULL);place_window(GTK_WINDOW(a),0,20);loaded(ta);
    Browser*b=new_browser();Tab*tb=add_tab(b,right);g_action_group_activate_action(G_ACTION_GROUP(b->actions),"list",NULL);place_window(GTK_WINDOW(b),710,20);loaded(tb);
    g_autofree char *alpha_mode_path = g_build_filename(argv[1], "alpha.txt", NULL);
    g_autofree char *beta_mode_path = g_build_filename(argv[1], "beta.txt", NULL);
    g_assert_cmpint(g_chmod(alpha_mode_path, 0644), ==, 0); g_assert_cmpint(g_chmod(beta_mode_path, 0600), ==, 0);
    loaded(ta);select_name(ta,"alpha.txt",TRUE);select_name(ta,"beta.txt",FALSE);properties(a);pump();
    GtkWindow*details=property_window(a);g_assert_nonnull(details);
    for(int i=0;i<100&&calculating(GTK_WIDGET(details));i++)pump();
    g_assert_false(calculating(GTK_WIDGET(details)));
    GtkWidget *apply_permissions = named_button(GTK_WIDGET(details), "Apply permissions");
    GtkWidget *close_properties = named_button(GTK_WIDGET(details), "Close");
    GtkWidget *execute = permission_check(GTK_WIDGET(details), 0100);
    GtkWidget *group_read = permission_check(GTK_WIDGET(details), 0040);
    g_assert_nonnull(apply_permissions); g_assert_nonnull(execute); g_assert_nonnull(group_read);
    g_assert_false(gtk_widget_has_css_class(close_properties, "suggested-action"));
    g_assert_true(gtk_check_button_get_inconsistent(GTK_CHECK_BUTTON(group_read)));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(execute), TRUE);
    g_assert_true(gtk_widget_get_sensitive(apply_permissions));
    g_signal_emit_by_name(apply_permissions, "clicked");pump();
    for (int i = 0; i < 20 && permission_mode(beta_mode_path) != 0700; ++i) pump();
    g_assert_cmpuint(permission_mode(alpha_mode_path), ==, 0744);
    g_assert_cmpuint(permission_mode(beta_mode_path), ==, 0700);
    g_assert_true(gtk_check_button_get_inconsistent(GTK_CHECK_BUTTON(group_read)));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(group_read), TRUE);
    g_signal_emit_by_name(apply_permissions, "clicked");pump();
    for (int i = 0; i < 20 && permission_mode(beta_mode_path) != 0740; ++i) pump();
    g_assert_cmpuint(permission_mode(alpha_mode_path), ==, 0744);
    g_assert_cmpuint(permission_mode(beta_mode_path), ==, 0740);
    /* Closing without Apply leaves the edited permission uncommitted. */
    gtk_check_button_set_active(GTK_CHECK_BUTTON(execute), FALSE);
    const char *capture = g_getenv("PCMANAGER_PROPERTIES_CAPTURE");
    if (capture) {
        g_autoptr(GSubprocess) shot = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, NULL, "import", "-window", "root", capture, NULL);
        g_assert_true(g_subprocess_wait_check(shot, NULL, NULL));
    }
    gtk_window_destroy(details);g_object_unref(details);pump();
    g_assert_cmpuint(permission_mode(alpha_mode_path), ==, 0744); g_assert_cmpuint(permission_mode(beta_mode_path), ==, 0740);
    g_assert_true(gtk_widget_get_mapped(GTK_WIDGET(a)));
    properties(a);details=property_window(a);g_assert_nonnull(details);
    gtk_window_destroy(details);g_object_unref(details);pump();
    g_assert_true(gtk_widget_get_mapped(GTK_WIDGET(a)));
    select_name(ta,"alpha.txt",TRUE);
    g_print("PASS: closing multi-file Properties after completion or while measuring keeps the browser open.\n");
    GtkWidget*source=find_row(ta->list,"alpha.txt");g_assert_nonnull(source);
    pointer_drag(source,tb->list,TRUE,FALSE);exists(argv[1],"alpha.txt",FALSE);exists(argv[2],"alpha.txt",TRUE);g_print("PASS: list drag moves to another window.\n");
    loaded(ta);loaded(tb);select_name(ta,"beta.txt",TRUE);select_name(ta,"gamma.txt",FALSE);pump();
    source=find_row(ta->list,"beta.txt");g_assert_nonnull(source);pointer_drag(source,tb->list,TRUE,FALSE);
    exists(argv[1],"beta.txt",FALSE);exists(argv[1],"gamma.txt",FALSE);exists(argv[2],"beta.txt",TRUE);exists(argv[2],"gamma.txt",TRUE);g_print("PASS: multi-selection drag preserves both files.\n");
    /* P1: a monitored change preserves selection and scroll offset. */
    for(int fi=0;fi<60;fi++){char*ff=g_strdup_printf("%s/fill-%02d.txt",argv[1],fi);g_file_set_contents(ff,"x",-1,NULL);g_free(ff);}
    int wj;for(wj=0;wj<120&&(!model_has(ta,"fill-59.txt")||gtk_directory_list_is_loading(ta->directory));wj++)pump();
    g_assert_cmpint(wj,<,120);
    select_name(ta,"fill-05.txt",TRUE);select_name(ta,"fill-06.txt",FALSE);pump();
    g_assert_true(is_selected(ta,"fill-05.txt"));g_assert_true(is_selected(ta,"fill-06.txt"));
    GtkAdjustment*vadj=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(gtk_widget_get_parent(ta->list)));
    /* Scroll to the bottom, forcing row measurement until the range settles. */
    for(int si=0;si<8;si++){gtk_adjustment_set_value(vadj,1e9);pump();pump();}
    double before=gtk_adjustment_get_value(vadj);
    g_assert_cmpfloat(before,>,100.0);
    char*fillnew=g_build_filename(argv[1],"fill-new.txt",NULL);g_file_set_contents(fillnew,"new",-1,NULL);
    int wi;for(wi=0;wi<120&&(!model_has(ta,"fill-new.txt")||gtk_directory_list_is_loading(ta->directory));wi++)pump();
    g_assert_cmpint(wi,<,120);for(int wk=0;wk<10;wk++)pump();
    g_assert_true(is_selected(ta,"fill-05.txt"));g_assert_true(is_selected(ta,"fill-06.txt"));
    /* The viewport must still show the bottom region, not jump to the top. */
    g_assert_cmpfloat(gtk_adjustment_get_value(vadj),>,before-100.0);
    g_print("PASS: monitored change preserves selection and scroll.\n");
    g_free(fillnew);
    loaded(ta);loaded(tb);g_action_group_activate_action(G_ACTION_GROUP(a->actions),"icons",NULL);g_action_group_activate_action(G_ACTION_GROUP(b->actions),"icons",NULL);pump();
    source=find_row(ta->grid,"folder");GtkWidget*dest=find_row(tb->grid,"nested");g_assert_nonnull(source);g_assert_nonnull(dest);
    pointer_drag(source,dest,FALSE,TRUE);exists(argv[1],"folder/payload.txt",TRUE);exists(argv[2],"nested/folder/payload.txt",TRUE);g_print("PASS: Ctrl-drag copies folder contents into a folder icon.\n");
    loaded(ta);loaded(tb);source=find_row(ta->grid,"folder");dest=find_row(tb->grid,"nested");pointer_drag(source,dest,FALSE,TRUE);
    exists(argv[1],"folder/payload.txt",TRUE);exists(argv[2],"nested/folder/payload.txt",TRUE);g_assert_true(gtk_widget_get_visible(b->message));g_print("PASS: existing destination refused without deleting source.\n");
    // A different application offers only text/uri-list, including escaped filenames.
    GtkWidget*external=gtk_window_new();GtkWidget*area=gtk_label_new("External URI file source / drop destination");
    gtk_window_set_child(GTK_WINDOW(external),area);gtk_window_set_default_size(GTK_WINDOW(external),650,130);gtk_window_present(GTK_WINDOW(external));pump();
    GdkSurface*es=gtk_native_get_surface(GTK_NATIVE(external));char*position=g_strdup_printf("windowmove %lu 10 700",gdk_x11_surface_get_xid(es));xdo(position);g_free(position);
    char*external_path=g_build_filename(argv[1],"external ü # file.txt",NULL);g_assert_true(g_file_set_contents(external_path,"external",-1,NULL));
    int w0;for(w0=0;w0<120&&(!model_has(ta,"external ü # file.txt")||gtk_directory_list_is_loading(ta->directory));w0++)pump();
    g_assert_cmpint(w0,<,120);
    GFile*external_file=g_file_new_for_path(external_path);char*uri=g_file_get_uri(external_file);char*uris=g_strconcat(uri,"\r\n",NULL);
    GBytes*bytes=g_bytes_new(uris,strlen(uris));GdkContentProvider*provider=gdk_content_provider_new_for_bytes("text/uri-list",bytes);g_bytes_unref(bytes);
    GtkDragSource*foreign=gtk_drag_source_new();gtk_drag_source_set_actions(foreign,GDK_ACTION_COPY);gtk_drag_source_set_content(foreign,provider);g_object_unref(provider);gtk_widget_add_controller(area,GTK_EVENT_CONTROLLER(foreign));
    pointer_drag(area,tb->grid,TRUE,FALSE);exists(argv[1],"external ü # file.txt",TRUE);exists(argv[2],"external ü # file.txt",TRUE);g_print("PASS: external URI-list import copies escaped filenames.\n");
    const char*types[]={"text/uri-list"};GtkDropTargetAsync*receiver=gtk_drop_target_async_new(gdk_content_formats_new(types,1),GDK_ACTION_COPY);
    g_signal_connect(receiver,"drag-enter",G_CALLBACK(export_motion),NULL);g_signal_connect(receiver,"drag-motion",G_CALLBACK(export_motion),NULL);g_signal_connect(receiver,"drop",G_CALLBACK(export_drop),NULL);gtk_widget_add_controller(area,GTK_EVENT_CONTROLLER(receiver));
    loaded(tb);source=find_row(tb->grid,"external ü # file.txt");g_assert_nonnull(source);pointer_drag(source,area,FALSE,FALSE);for(int i=0;!exported_uris&&i<30;i++)pump();
    char*destpath=g_build_filename(argv[2],"external ü # file.txt",NULL);GFile*destfile=g_file_new_for_path(destpath);char*desturi=g_file_get_uri(destfile);
    g_assert_nonnull(exported_uris);g_assert_nonnull(strstr(exported_uris,desturi));exists(argv[2],"external ü # file.txt",TRUE);g_print("PASS: export to URI-only external drop target preserves source.\n");
    /* Step 10b: the same external window now accepts MOVE. It copies the file
     * itself, so the source must trash its copy (recoverable, not deleted). */
    gtk_widget_remove_controller(area,GTK_EVENT_CONTROLLER(receiver));
    const char*move_types[]={"text/uri-list"};GtkDropTargetAsync*move_receiver=gtk_drop_target_async_new(gdk_content_formats_new(move_types,1),GDK_ACTION_COPY|GDK_ACTION_MOVE);
    g_signal_connect(move_receiver,"drag-enter",G_CALLBACK(move_motion),NULL);g_signal_connect(move_receiver,"drag-motion",G_CALLBACK(move_motion),NULL);g_signal_connect(move_receiver,"drop",G_CALLBACK(move_drop),NULL);gtk_widget_add_controller(area,GTK_EVENT_CONTROLLER(move_receiver));
    move_target_dir=g_build_filename(argv[2],"move-target",NULL);g_assert_cmpint(g_mkdir(move_target_dir,0700),==,0);
    int w1;for(w1=0;w1<120&&(!model_has(tb,"move-target")||gtk_directory_list_is_loading(tb->directory));w1++)pump();
    g_assert_cmpint(w1,<,120);
    loaded(tb);source=find_row(tb->grid,"external ü # file.txt");g_assert_nonnull(source);pointer_drag(source,area,FALSE,FALSE);
    char*srcdir_path=g_build_filename(argv[2],"external ü # file.txt",NULL);
    for(int i=0;i<60&&g_file_test(srcdir_path,G_FILE_TEST_EXISTS);i++)pump();
    char*gotpath=g_build_filename(move_target_dir,"external ü # file.txt",NULL);
    g_assert_true(g_file_test(gotpath,G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(srcdir_path,G_FILE_TEST_EXISTS));
    char*trashpath=g_build_filename(g_get_user_data_dir(),"Trash","files","external ü # file.txt",NULL);
    g_assert_true(g_file_test(trashpath,G_FILE_TEST_EXISTS));
    g_print("PASS: external MOVE destination copies file and source is trashed.\n");
    g_free(move_target_dir);g_free(srcdir_path);g_free(gotpath);g_free(trashpath);
    gtk_window_destroy(GTK_WINDOW(external));g_free(external_path);g_object_unref(external_file);g_free(uri);g_free(uris);g_free(destpath);g_object_unref(destfile);g_free(desturi);g_free(exported_uris);
    GFile*tabdest=g_file_get_child(right,"tab-dest");g_assert_true(g_file_make_directory(tabdest,NULL,NULL));
    Tab*extra=add_tab(b,tabdest);loaded(extra);gtk_notebook_set_current_page(GTK_NOTEBOOK(b->notebook),0);pump();
    GtkWidget*tablabel=gtk_notebook_get_tab_label(GTK_NOTEBOOK(b->notebook),GTK_WIDGET(extra));
    /* Use fresh top-sorted rows: with 60+ items the grid virtualizes, and
     * pressing a just-realized bottom-edge row with synthetic events does
     * not reliably start drags (GTK recycles the item mid-realization).
     * Sorting puts "aaa.txt" first, so it is always in the measured top
     * viewport regardless of the scroll position left by earlier tests. */
    char*aaapath=g_build_filename(argv[1],"aaa.txt",NULL);g_assert_true(g_file_set_contents(aaapath,"aaa",-1,NULL));
    int wa;for(wa=0;wa<120&&(!model_has(ta,"aaa.txt")||gtk_directory_list_is_loading(ta->directory));wa++)pump();
    g_assert_cmpint(wa,<,120);
    source=stable_row(ta,ta->grid,"aaa.txt");g_assert_nonnull(source);pointer_drag(source,tablabel,FALSE,TRUE);
    exists(argv[2],"tab-dest/aaa.txt",TRUE);g_print("PASS: dropping onto an inactive tab uses that tab's folder.\n");
    g_free(aaapath);
    GFile*placedest=g_file_get_child(right,"place-dest");g_assert_true(g_file_make_directory(placedest,NULL,NULL));
    place_add(b,"Test drop location","folder",placedest,NULL,NULL);pump();GtkWidget*place=gtk_widget_get_last_child(b->places);
    char*aabpath=g_build_filename(argv[1],"aab.txt",NULL);g_assert_true(g_file_set_contents(aabpath,"aab",-1,NULL));
    int wb;for(wb=0;wb<120&&(!model_has(ta,"aab.txt")||gtk_directory_list_is_loading(ta->directory));wb++)pump();
    g_assert_cmpint(wb,<,120);
    source=stable_row(ta,ta->grid,"aab.txt");g_assert_nonnull(source);pointer_drag(source,place,FALSE,TRUE);exists(argv[2],"place-dest/aab.txt",TRUE);
    g_print("PASS: dropping onto a Places row copies into that location.\n");
    g_free(aabpath);g_object_unref(tabdest);g_object_unref(placedest);
    /* P1: cut-paste into the Trash tab trashes the sources. */
    GFile*trashloc=g_file_new_for_uri("trash:///");
    Tab*trashtab=add_tab(b,trashloc);g_object_unref(trashloc);loaded(trashtab);
    /* Cut while the source tab is current, then paste into the Trash tab. */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(b->notebook),gtk_notebook_page_num(GTK_NOTEBOOK(b->notebook),GTK_WIDGET(tb)));
    select_name(tb,"gamma.txt",TRUE);pump();copy_files(b,TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(b->notebook),gtk_notebook_page_num(GTK_NOTEBOOK(b->notebook),GTK_WIDGET(trashtab)));
    g_action_group_activate_action(G_ACTION_GROUP(b->actions),"paste",NULL);
    GtkDialog *trash_confirmation=NULL;
    for(int i=0;i<30 && !trash_confirmation;i++) {
        pump(); GListModel *windows=gtk_window_get_toplevels();
        for(guint j=0;j<g_list_model_get_n_items(windows);j++) {
            GtkWidget *window=g_list_model_get_item(windows,j);
            if(GTK_IS_MESSAGE_DIALOG(window)) trash_confirmation=GTK_DIALOG(window);
            else g_object_unref(window);
        }
    }
    g_assert_nonnull(trash_confirmation);
    gtk_dialog_response(trash_confirmation,1); g_object_unref(trash_confirmation);
    char*gammasrc=g_build_filename(argv[2],"gamma.txt",NULL);
    char*trashed=g_build_filename(g_get_user_data_dir(),"Trash","files","gamma.txt",NULL);
    int pi;for(pi=0;pi<120&&(g_file_test(gammasrc,G_FILE_TEST_EXISTS)||!g_file_test(trashed,G_FILE_TEST_EXISTS));pi++)pump();
    g_assert_cmpint(pi,<,120);
    g_print("PASS: cut-paste into Trash trashes the source.\n");
    g_free(gammasrc);g_free(trashed);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(b->notebook),0);
    gtk_window_close(GTK_WINDOW(a));gtk_window_close(GTK_WINDOW(b));pump();g_assert_cmpuint(pcmgr_browser_count(),==,0);
    g_object_unref(left);g_object_unref(right);fm_finalize();
}
