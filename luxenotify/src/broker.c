/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Stable public bus ownership, with an expendable GTK renderer on a private
 * authenticated socket. No public second renderer service or bus-name gap. */
#include <gio/gio.h>
#include <glib-unix.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include "notification-protocol.h"

#define BUS LUXE_NOTIFY_BUS_NAME
#define PATH LUXE_NOTIFY_OBJECT_PATH
#define IFACE LUXE_NOTIFY_INTERFACE
#define CONTROL LUXE_NOTIFY_CONTROL_INTERFACE
typedef struct {
    GDBusMethodInvocation *inv;
    GVariant *args;
    char *name;
} Request;
typedef struct {
    GMainLoop *loop;
    GDBusConnection *bus, *peer;
    GSubprocess *child;
    GCancellable *starting;
    GDBusNodeInfo *info;
    GQueue queue;
    /* Renderer owns live cards/count; these broker caches serve synchronous
     * control calls and permit close notifications when the child exits. */
    GHashTable *active;
    guint owner, reg[2], idle, deadline, count, next_id;
    gsize queued_bytes;
    gboolean busy, stopping, shutdown;
    char *renderer;
} Broker;
static void drain(Broker *b);
static void free_request(Request *r)
{
    g_object_unref(r->inv);
    g_variant_unref(r->args);
    g_free(r->name);
    g_free(r);
}
static void fail_queue(Broker *b,const char *message)
{
    Request *r;
    while((r=g_queue_pop_head(&b->queue))) {
        g_dbus_method_invocation_return_dbus_error(r->inv, IFACE ".Error.Failed", message);
        free_request(r);
    }
    b->queued_bytes=0;
}
static void signal_public(Broker *b,const char *iface,const char *name,GVariant *args)
{
    g_dbus_connection_emit_signal(b->bus, NULL, PATH, iface, name, args, NULL);
}
static gboolean idle_stop(gpointer data)
{
    Broker *b=data;b->idle=0;
    if(!b->count&&!b->busy&&g_queue_is_empty(&b->queue)&&b->child) {
        b->stopping=TRUE;g_subprocess_force_exit(b->child);
    }
    return G_SOURCE_REMOVE;
}
static void schedule(Broker *b)
{if(!b->count&&!b->busy&&g_queue_is_empty(&b->queue)&&b->child&&!b->idle&&!b->stopping)b->idle=g_timeout_add_seconds(2,idle_stop,b);}
static void child_signal(GDBusConnection *peer,const char *sender,const char *path,const char *iface,const char *name,GVariant *args,gpointer data)
{
    (void)peer;
    (void)sender;
    (void)path;
    Broker *b = data;

    if (g_str_equal(iface, CONTROL) && g_str_equal(name, "ActiveCountChanged"))
        g_variant_get(args, "(u)", &b->count);
    if (g_str_equal(iface, IFACE) && g_str_equal(name, "NotificationClosed")) {
        guint id, reason;
        g_variant_get(args, "(uu)", &id, &reason);
        g_hash_table_remove(b->active, GUINT_TO_POINTER(id));
    }
    signal_public(b, iface, name, args);
    schedule(b);
}
static void reaped(GObject *child,GAsyncResult *result,gpointer data)
{
    Broker *b = data;
    g_subprocess_wait_finish(G_SUBPROCESS(child), result, NULL);
    if (b->deadline) {
        g_source_remove(b->deadline);
        b->deadline = 0;
    }
    if (b->starting)
        g_cancellable_cancel(b->starting);
    g_clear_object(&b->child);
    if (b->peer)
        g_dbus_connection_close_sync(b->peer, NULL, NULL);
    g_clear_object(&b->peer);
    b->stopping = FALSE;

    /* No card can survive its renderer. Publish one close event per cached ID
     * before accepting work on a replacement child. */
    GHashTableIter iter;
    gpointer id;
    g_hash_table_iter_init(&iter, b->active);
    while (g_hash_table_iter_next(&iter, &id, NULL))
        signal_public(b, IFACE, "NotificationClosed",
                      g_variant_new("(uu)", GPOINTER_TO_UINT(id), 3));
    g_hash_table_remove_all(b->active);
    if (b->count) {
        b->count = 0;
        signal_public(b, CONTROL, "ActiveCountChanged", g_variant_new("(u)", 0));
    }
    if (!b->shutdown)
        drain(b);
}
static gboolean deadline(gpointer data)
{Broker *b=data;b->deadline=0;if(b->starting)g_cancellable_cancel(b->starting);return G_SOURCE_REMOVE;}
static void connected(GObject *object,GAsyncResult *result,gpointer data)
{
    (void)object;Broker *b=data;g_autoptr(GError) error=NULL;
    GDBusConnection *peer=g_dbus_connection_new_finish(result,&error);
    if(b->deadline){g_source_remove(b->deadline);b->deadline=0;}
    g_clear_object(&b->starting);
    if(!peer || !b->child || b->shutdown) {
        if(peer)g_object_unref(peer);
        fail_queue(b,error?error->message:"Notification renderer exited");
        if(b->child){b->stopping=TRUE;g_subprocess_force_exit(b->child);}return;
    }
    b->peer=peer;
    g_dbus_connection_signal_subscribe(peer,NULL,NULL,NULL,PATH,NULL,G_DBUS_SIGNAL_FLAGS_NONE,child_signal,b,NULL);
    drain(b);
}
static gboolean same_user(GDBusAuthObserver *observer,GIOStream *stream,GCredentials *credentials,gpointer data)
{(void)observer;(void)stream;(void)data;return credentials&&g_credentials_get_unix_user(credentials,NULL)==getuid();}
static void start(Broker *b)
{
    int sockets[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sockets)){fail_queue(b,"Cannot open notification transport");return;}
    g_autoptr(GError) error=NULL;
    g_autoptr(GSocket) socket=g_socket_new_from_fd(sockets[0],&error);
    if(!socket){close(sockets[0]);close(sockets[1]);fail_queue(b,error->message);return;}
    g_autoptr(GSocketConnection) stream=g_socket_connection_factory_create_connection(socket);
    g_autoptr(GSubprocessLauncher) launch=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_take_fd(launch,sockets[1],3);
    g_autofree char *base=g_strdup_printf("%u",b->next_id);
    b->child=g_subprocess_launcher_spawn(launch,&error,b->renderer,"--peer-fd","3","--id-base",base,NULL);
    if(!b->child){fail_queue(b,error->message);return;}
    g_subprocess_wait_async(b->child,NULL,reaped,b);
    b->starting=g_cancellable_new();
    g_autofree char *guid=g_dbus_generate_guid();
    g_autoptr(GDBusAuthObserver) observer=g_dbus_auth_observer_new();
    g_signal_connect(observer,"authorize-authenticated-peer",G_CALLBACK(same_user),NULL);
    g_dbus_connection_new(G_IO_STREAM(stream),guid,G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_SERVER,observer,b->starting,connected,b);
    b->deadline=g_timeout_add_seconds(5,deadline,b);
}
typedef struct {Broker *broker;Request *request;} Flight;
static void replied(GObject *object,GAsyncResult *result,gpointer data)
{
    Flight *flight = data;
    Broker *b = flight->broker;
    Request *r = flight->request;
    g_autoptr(GError) error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, &error);
    gboolean current_peer = b->peer == G_DBUS_CONNECTION(object);

    if (reply && b->child && current_peer) {
        if (g_str_equal(r->name, "Notify")) {
            guint id;
            g_variant_get(reply, "(u)", &id);
            if (id > b->next_id || b->next_id == G_MAXUINT)
                b->next_id = id;
            g_hash_table_add(b->active, GUINT_TO_POINTER(id));
        }
        g_dbus_method_invocation_return_value(r->inv, reply);
    } else {
        if (reply)
            g_variant_unref(reply);
        /* A remote method error is a completed rejection. A local transport
         * failure or timeout leaves the outcome unknown and the peer unsafe
         * to reuse; wait for its reap before starting another renderer. */
        if (error && !g_dbus_error_is_remote_error(error) && current_peer && b->child) {
            b->stopping = TRUE;
            g_subprocess_force_exit(b->child);
        }
        if (error)
            g_dbus_method_invocation_return_gerror(r->inv, error);
        else
            g_dbus_method_invocation_return_dbus_error(
                r->inv, IFACE ".Error.Failed", "Notification renderer exited");
    }
    free_request(r);
    g_free(flight);
    b->busy = FALSE;
    if (!b->shutdown) {
        drain(b);
        schedule(b);
    }
}
static void drain(Broker *b)
{
    if (b->shutdown || b->busy || b->stopping || g_queue_is_empty(&b->queue))
        return;
    if (!b->peer) {
        if (!b->child && !b->starting)
            start(b);
        return;
    }

    Request *r = g_queue_pop_head(&b->queue);
    b->queued_bytes -= g_variant_get_size(r->args);
    b->busy = TRUE;
    Flight *flight = g_new0(Flight, 1);
    flight->broker = b;
    flight->request = r;
    g_dbus_connection_call(b->peer, NULL, PATH, IFACE, r->name, r->args, NULL,
                           G_DBUS_CALL_FLAGS_NONE, 5000, NULL, replied, flight);
}
static void method(GDBusConnection *bus,const char *sender,const char *path,const char *iface,const char *name,GVariant *args,GDBusMethodInvocation *inv,gpointer data)
{
    (void)bus;
    (void)sender;
    (void)path;
    (void)iface;
    Broker *b = data;

    if (g_str_equal(name, "GetCapabilities")) {
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(^as)", LUXE_NOTIFY_CAPABILITIES));
        return;
    }
    if (g_str_equal(name, "GetServerInformation")) {
        g_dbus_method_invocation_return_value(inv,
            g_variant_new("(ssss)", LUXE_NOTIFY_SERVER_NAME,
                          LUXE_NOTIFY_SERVER_VENDOR, VERSION,
                          LUXE_NOTIFY_SPEC_VERSION));
        return;
    }
    if (g_str_equal(name, "GetActiveCount")) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", b->count));
        return;
    }
    if (g_str_equal(name, "CloseNotification") && !b->child &&
        g_queue_is_empty(&b->queue)) {
        g_dbus_method_invocation_return_value(inv, NULL);
        return;
    }

    gsize size = g_variant_get_size(args);
    if (size > 16 * 1024 * 1024 || b->queued_bytes + size > 32 * 1024 * 1024 ||
        g_queue_get_length(&b->queue) >= 64) {
        g_dbus_method_invocation_return_dbus_error(
            inv, IFACE ".Error.Failed", "Notification queue limit exceeded");
        return;
    }
    if (b->idle) {
        g_source_remove(b->idle);
        b->idle = 0;
    }
    Request *r = g_new0(Request, 1);
    r->inv = g_object_ref(inv);
    r->args = g_variant_ref(args);
    r->name = g_strdup(name);
    g_queue_push_tail(&b->queue, r);
    b->queued_bytes += size;
    drain(b);
}
static const GDBusInterfaceVTable vtable={.method_call=method};
static void acquired(GDBusConnection *bus,const char *name,gpointer data)
{
    (void)name;Broker *b=data;b->bus=g_object_ref(bus);
    for(guint i=0;i<2;i++)b->reg[i]=g_dbus_connection_register_object(bus,PATH,b->info->interfaces[i],&vtable,b,NULL,NULL);
    if(!b->reg[0]||!b->reg[1])g_main_loop_quit(b->loop);
}
static void lost(GDBusConnection *bus,const char *name,gpointer data)
{(void)bus;(void)name;g_main_loop_quit(((Broker *)data)->loop);}
static gboolean stop(gpointer data){g_main_loop_quit(((Broker *)data)->loop);return G_SOURCE_CONTINUE;}
int main(int argc,char **argv)
{
    Broker b={.renderer=RENDERER_PATH};
    if(argc==3&&g_str_equal(argv[1],"--renderer"))b.renderer=argv[2];
    else if(argc>1){
        char **renderer_argv=g_new0(char *,(gsize)argc+1);
        renderer_argv[0]=(char *)RENDERER_PATH;
        for(int i=1;i<argc;i++)renderer_argv[i]=argv[i];
        execv(RENDERER_PATH,renderer_argv);
        g_printerr("luxenotify: cannot start renderer: %s\n",g_strerror(errno));
        g_free(renderer_argv);
        return 1;
    }
    b.loop=g_main_loop_new(NULL,FALSE);b.info=g_dbus_node_info_new_for_xml(LUXE_NOTIFY_INTROSPECTION_XML,NULL);b.active=g_hash_table_new(g_direct_hash,g_direct_equal);
    guint term=g_unix_signal_add(SIGTERM,stop,&b),intr=g_unix_signal_add(SIGINT,stop,&b);
    b.owner=g_bus_own_name(G_BUS_TYPE_SESSION,BUS,G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,acquired,NULL,lost,&b,NULL);
    g_main_loop_run(b.loop);b.shutdown=TRUE;
    if(b.idle)g_source_remove(b.idle);
    if(b.deadline){g_source_remove(b.deadline);b.deadline=0;}
    if(b.starting)g_cancellable_cancel(b.starting);
    if(b.child)g_subprocess_force_exit(b.child);
    if(b.peer)g_dbus_connection_close_sync(b.peer,NULL,NULL);
    while(b.child||b.busy||b.starting)g_main_context_iteration(NULL,TRUE);
    fail_queue(&b,"Notification service stopped");
    for(guint i=0;i<2;i++)if(b.reg[i]&&b.bus)g_dbus_connection_unregister_object(b.bus,b.reg[i]);
    g_bus_unown_name(b.owner);g_clear_object(&b.bus);g_hash_table_unref(b.active);g_dbus_node_info_unref(b.info);g_main_loop_unref(b.loop);
    g_source_remove(term);g_source_remove(intr);return 0;
}
