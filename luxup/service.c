/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "check.h"
#include "tray.h"
#include <glib-unix.h>
#include <signal.h>

static const char xml[]="<node><interface name='org.luxe.LuxUp.Service'>"
"<method name='GetState'><arg direction='out' type='a{sv}'/></method>"
"<method name='Check'><arg direction='in' type='b'/></method>"
"<method name='Cancel'/><method name='Quit'/>"
"<signal name='Changed'><arg type='a{sv}'/></signal></interface></node>";
typedef struct {
    GApplication *app; LuxUpTray *tray; LuxUpResult *result;
    GCancellable *cancel; gboolean checking,stopping,queued;
    guint registration,timer; GDBusNodeInfo *info;GDBusConnection *bus;
} Service;
static GVariant *state(Service *s)
{
    GVariantBuilder values,packages,packages_v2;
    g_variant_builder_init(&values,G_VARIANT_TYPE_VARDICT);
    g_variant_builder_init(&packages,G_VARIANT_TYPE("a(isss)"));
    g_variant_builder_init(&packages_v2,G_VARIANT_TYPE("a(isssss)"));
    if(s->result) for(guint i=0;i<s->result->packages->len;i++) {
        LuxUpPackage *p=g_ptr_array_index(s->result->packages,i);
        g_variant_builder_add(&packages,"(isss)",p->source,p->name,p->installed,p->available);
        g_variant_builder_add(&packages_v2,"(isssss)",p->source,p->name,p->installed,p->available,
                              p->scope?p->scope:"",p->ref?p->ref:"");
    }
    g_variant_builder_add(&values,"{sv}","packages",g_variant_builder_end(&packages));
    g_variant_builder_add(&values,"{sv}","packages-v2",g_variant_builder_end(&packages_v2));
    g_variant_builder_add(&values,"{sv}","checking",g_variant_new_boolean(s->checking));
    g_variant_builder_add(&values,"{sv}","has-result",g_variant_new_boolean(s->result!=NULL));
    g_variant_builder_add(&values,"{sv}","failed",g_variant_new_boolean(s->result&&s->result->check_failed));
    g_variant_builder_add(&values,"{sv}","apt-refreshed-at",g_variant_new_int64(s->result?s->result->apt_refreshed_at:0));
    g_variant_builder_add(&values,"{sv}","apt-available",g_variant_new_boolean(s->result&&s->result->apt_available));
    g_variant_builder_add(&values,"{sv}","snap-available",g_variant_new_boolean(s->result&&s->result->snap_available));
    g_variant_builder_add(&values,"{sv}","snap-held",g_variant_new_boolean(s->result&&s->result->snap_auto_refresh_held));
    g_variant_builder_add(&values,"{sv}","snap-hold-known",g_variant_new_boolean(s->result&&s->result->snap_hold_known));
    g_variant_builder_add(&values,"{sv}","error",g_variant_new_string(s->result&&s->result->apt_refresh_error?s->result->apt_refresh_error:""));
    g_variant_builder_add(&values,"{sv}","apt-check-error",g_variant_new_string(s->result&&s->result->apt_check_error?s->result->apt_check_error:""));
    g_variant_builder_add(&values,"{sv}","flatpak-user-error",g_variant_new_string(s->result&&s->result->flatpak_user_error?s->result->flatpak_user_error:""));
    g_variant_builder_add(&values,"{sv}","flatpak-system-error",g_variant_new_string(s->result&&s->result->flatpak_system_error?s->result->flatpak_system_error:""));
    g_variant_builder_add(&values,"{sv}","snap-check-error",g_variant_new_string(s->result&&s->result->snap_check_error?s->result->snap_check_error:""));
    return g_variant_builder_end(&values);
}
static void publish(Service *s)
{
    guint total=s->result?s->result->packages->len:0;
    gboolean fresh=luxup_apt_lists_fresh(s->result,g_get_real_time()/G_USEC_PER_SEC);
    LuxUpTrayState icon=s->checking?LUXUP_TRAY_CHECKING:!s->result?LUXUP_TRAY_UNKNOWN:total?LUXUP_TRAY_UPDATES:(s->result->check_failed||!fresh)?LUXUP_TRAY_UNKNOWN:LUXUP_TRAY_UPTODATE;
    luxup_tray_set_state(s->tray,icon);
    g_autofree char *tip=s->checking?g_strdup("LuxUp — checking for updates…"):icon==LUXUP_TRAY_UNKNOWN?g_strdup("LuxUp — status unknown"):total?g_strdup_printf(s->result->check_failed?"LuxUp — %u updates found; some checks failed":"LuxUp — %u updates available",total):g_strdup("LuxUp — up to date");
    luxup_tray_set_tooltip(s->tray,tip);
    g_dbus_connection_emit_signal(s->bus,NULL,"/org/luxe/LuxUp","org.luxe.LuxUp.Service","Changed",g_variant_new("(@a{sv})",state(s)),NULL);
}
static void check(Service *s,gboolean refresh);
static void completed(GObject *source,GAsyncResult *result,gpointer data)
{
    (void)source; Service *s=data; g_autoptr(GError) error=NULL;
    LuxUpResult *next=g_task_propagate_pointer(G_TASK(result),&error);
    s->checking=FALSE; g_clear_object(&s->cancel);
    if(next) {
        if (next->apt_available && !next->apt_refreshed_at && !next->apt_refresh_error && s->result)
            next->apt_refreshed_at=s->result->apt_refreshed_at;
        luxup_result_free(s->result);s->result=next;
    }
    if(!s->stopping) {publish(s);if(s->queued){s->queued=FALSE;check(s,TRUE);}}
}
static void worker(GTask *task,gpointer object,gpointer data,GCancellable *cancel)
{
    (void)object;
    LuxUpResult *result=luxup_check(GPOINTER_TO_INT(data),cancel);
    if(g_task_return_error_if_cancelled(task)) luxup_result_free(result);
    else g_task_return_pointer(task,result,(GDestroyNotify)luxup_result_free);
}
static void check(Service *s,gboolean refresh)
{
    if(s->checking) {if(refresh){s->queued=TRUE;g_cancellable_cancel(s->cancel);}return;}
    s->checking=TRUE; s->cancel=g_cancellable_new(); publish(s);
    GTask *task=g_task_new(NULL,s->cancel,completed,s);
    g_task_set_task_data(task,GINT_TO_POINTER(refresh),NULL);g_task_run_in_thread(task,worker);g_object_unref(task);
}
static void method(GDBusConnection *bus,const char *sender,const char *path,const char *iface,const char *name,GVariant *args,GDBusMethodInvocation *inv,gpointer data)
{
    (void)bus;(void)sender;(void)path;(void)iface;Service *s=data;
    if(g_str_equal(name,"GetState")) {g_dbus_method_invocation_return_value(inv,g_variant_new("(@a{sv})",state(s)));return;}
    if(g_str_equal(name,"Check")) {gboolean refresh;g_variant_get(args,"(b)",&refresh);check(s,refresh);}
    else if(g_str_equal(name,"Cancel")) {s->queued=FALSE;if(s->cancel)g_cancellable_cancel(s->cancel);}
    else if(g_str_equal(name,"Quit")) g_application_quit(s->app);
    g_dbus_method_invocation_return_value(inv,NULL);
}
static const GDBusInterfaceVTable vtable={.method_call=method};
static void clicked(LuxUpTray *tray,guint button,gpointer data)
{
    (void)tray; Service *s=data;
    if(button==2){check(s,TRUE);return;}
    g_autoptr(GError) error=NULL;
    g_autoptr(GSubprocess) child=g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,&error,LUXUP_UI,"--no-refresh",NULL);
    if(!child)g_warning("Cannot open LuxUp: %s",error->message);
}
static gboolean periodic(gpointer data) {check(data,FALSE);return G_SOURCE_CONTINUE;}
static void startup(GApplication *app,gpointer data)
{
    Service *s=data;g_application_hold(app);
    s->info=g_dbus_node_info_new_for_xml(xml,NULL);
    g_autoptr(GError) error=NULL;
    s->bus=g_object_ref(g_application_get_dbus_connection(app));
    s->registration=g_dbus_connection_register_object(s->bus,"/org/luxe/LuxUp",s->info->interfaces[0],&vtable,s,NULL,&error);
    if(!s->registration){g_warning("%s",error->message);g_application_quit(app);return;}
    s->tray=luxup_tray_new(clicked,s);s->timer=g_timeout_add_seconds(21600,periodic,s);
}
static int command(GApplication *app,GApplicationCommandLine *line,gpointer data)
{
    (void)app;Service *s=data;int argc;g_auto(GStrv) argv=g_application_command_line_get_arguments(line,&argc);
    gboolean refresh=TRUE;
    for(int i=1;i<argc;i++)if(g_str_equal(argv[i],"--no-refresh"))refresh=FALSE;
    if(!s->result&&!s->checking)check(s,refresh);
    return 0;
}
static gboolean stop(gpointer app){g_application_quit(app);return G_SOURCE_CONTINUE;}
int main(int argc,char **argv)
{
    Service s={0};s.app=g_application_new("org.luxe.LuxUp.Service",G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(s.app,"startup",G_CALLBACK(startup),&s);
    g_signal_connect(s.app,"command-line",G_CALLBACK(command),&s);
    guint term=g_unix_signal_add(SIGTERM,stop,s.app),intr=g_unix_signal_add(SIGINT,stop,s.app);
    int status=g_application_run(s.app,argc,argv);
    s.stopping=TRUE;if(s.cancel)g_cancellable_cancel(s.cancel);
    while(s.checking)g_main_context_iteration(NULL,TRUE);
    if(s.timer)g_source_remove(s.timer);
    if(s.registration)g_dbus_connection_unregister_object(s.bus,s.registration);
    luxup_tray_free(s.tray);luxup_result_free(s.result);
    g_clear_object(&s.cancel);g_clear_pointer(&s.info,g_dbus_node_info_unref);
    g_clear_object(&s.bus);g_source_remove(term);g_source_remove(intr);g_object_unref(s.app);return status;
}
