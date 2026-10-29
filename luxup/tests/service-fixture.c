/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Real coordinator, with a deterministic backend: no live package queries. */
#include <gio/gio.h>
typedef struct { GDBusMethodInvocation *invocation; GVariant *value; } DelayedState;
static gboolean return_delayed_state(gpointer data)
{
    DelayedState *delayed=data;
    g_dbus_method_invocation_return_value(delayed->invocation, delayed->value);
    g_variant_unref(delayed->value);
    g_free(delayed);
    return G_SOURCE_REMOVE;
}
static void fixture_return_value(GDBusMethodInvocation *invocation, GVariant *value)
{
    const char *sentinel=g_getenv("LUXUP_TEST_DELAY_STATE");
    if (sentinel && g_file_test(sentinel, G_FILE_TEST_EXISTS) &&
        g_str_equal(g_dbus_method_invocation_get_method_name(invocation), "GetState")) {
        DelayedState *delayed=g_new0(DelayedState,1);
        delayed->invocation=invocation; delayed->value=g_variant_ref_sink(value);
        g_timeout_add(2500, return_delayed_state, delayed);
    } else g_dbus_method_invocation_return_value(invocation,value);
}
#define g_dbus_method_invocation_return_value fixture_return_value
#include "../service.c"
#undef g_dbus_method_invocation_return_value
static void destroy_package(gpointer data)
{LuxUpPackage *p=data;g_free(p->name);g_free(p->installed);g_free(p->available);g_free(p->scope);g_free(p->ref);g_free(p);}
LuxUpResult *luxup_check(gboolean refresh,GCancellable *cancel)
{
    for(guint i=0;i<10&&!g_cancellable_is_cancelled(cancel);i++)g_usleep(10000);
    LuxUpResult *r=g_new0(LuxUpResult,1);r->packages=g_ptr_array_new_with_free_func(destroy_package);
    r->apt_available=TRUE;
    if (refresh) r->apt_refreshed_at=g_get_real_time()/G_USEC_PER_SEC;
    LuxUpPackage *p=g_new0(LuxUpPackage,1);p->source=1;p->name=g_strdup(refresh?"refreshed-fixture":"cached-fixture");p->installed=g_strdup("1");p->available=g_strdup("2");p->scope=g_strdup("user");p->ref=g_strdup("app/org.test.App/x86_64/stable");
    g_ptr_array_add(r->packages,p);return r;
}
void luxup_result_free(LuxUpResult *r)
{if(!r)return;g_ptr_array_unref(r->packages);g_free(r->apt_refresh_error);g_free(r->apt_check_error);g_free(r->flatpak_user_error);g_free(r->flatpak_system_error);g_free(r->snap_check_error);g_free(r);}
