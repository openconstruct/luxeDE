/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
static guint finish_count;
static GdkDragAction finished_action;
static void fixture_drop_finish(GdkDrop *drop,GdkDragAction action)
{ (void)drop;finish_count++;finished_action=action; }
#define gdk_drop_finish fixture_drop_finish
#include "../plugins/launchtaskbar.c"
#undef gdk_drop_finish
void x11_root_watch_window(Window window) { (void)window; }

int main(void)
{
    const guint8 launcher_path[]="/tmp/example.desktop";
    const guint8 uri_list[]="file:///tmp/example.desktop\r\n";
    const guint8 invalid_uri[]="not a uri\r\n";
    const guint8 embedded_nul[]={'/','t','m','p',0,'x'};
    g_assert_true(fm_init(NULL));
    FmPath *parsed=ltb_drop_parse_payload(launcher_path,sizeof(launcher_path)-1,TRUE);
    g_assert_nonnull(parsed);fm_path_unref(parsed);
    parsed=ltb_drop_parse_payload(uri_list,sizeof(uri_list)-1,FALSE);
    g_assert_nonnull(parsed);fm_path_unref(parsed);
    g_assert_null(ltb_drop_parse_payload(invalid_uri,sizeof(invalid_uri)-1,FALSE));
    g_assert_null(ltb_drop_parse_payload(embedded_nul,sizeof(embedded_nul),TRUE));
    g_assert_null(ltb_drop_parse_payload((const guint8 *)"",0,TRUE));

    /* A real async stream completion must remain safe after plugin teardown. */
    LaunchTaskBarPlugin *lb=g_new0(LaunchTaskBarPlugin,1);
    lb->drop_cancel=g_cancellable_new();
    LtbDropCtx *ctx=g_new0(LtbDropCtx,1);
    ctx->lb=lb;ctx->drop=(GdkDrop *)g_object_new(G_TYPE_OBJECT,NULL);
    ctx->cancel=g_object_ref(lb->drop_cancel);ctx->bytes=g_byte_array_new();
    ctx->stream=g_memory_input_stream_new_from_data("/tmp/launcher",14,NULL);
    g_input_stream_read_bytes_async(ctx->stream,8192,G_PRIORITY_DEFAULT,
        ctx->cancel,on_launchbar_drop_chunk,ctx);
    launchtaskbar_destructor(lb);
    while(finish_count==0)
        g_main_context_iteration(NULL,TRUE);
    g_assert_cmpuint(finish_count,==,1);g_assert_cmpint(finished_action,==,0);
    /* A chunk crossing the byte limit must be rejected before it is copied. */
    lb=g_new0(LaunchTaskBarPlugin,1);lb->drop_cancel=g_cancellable_new();
    ctx=g_new0(LtbDropCtx,1);ctx->lb=lb;
    ctx->drop=(GdkDrop *)g_object_new(G_TYPE_OBJECT,NULL);
    ctx->cancel=g_object_ref(lb->drop_cancel);ctx->bytes=g_byte_array_new();
    guint8 *oversized=g_malloc0(256*1024+1);
    g_assert_false(ltb_drop_append_bytes(ctx,oversized,256*1024+1));
    g_free(oversized);
    ltb_drop_finish(ctx,NULL);
    g_assert_cmpuint(finish_count,==,2);g_assert_cmpint(finished_action,==,0);
    launchtaskbar_destructor(lb);
    fm_finalize();
    return 0;
}
