/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxe-gtk-ui.h"
#include <glib/gstdio.h>
#include <string.h>
static gboolean fail_refresh;
static gboolean malformed_apt;
static gboolean empty_apt;
static int snap_time_mode;
static gboolean fail_apt_probe, fail_flatpak_user, fail_flatpak_system, fail_snap_list;
static gboolean apt_installed = TRUE;
static guint apt_calls;
static gint slow_apt_probe, slow_apt_entered;
static GString *captured_print;
static void capture_print(const gchar *text) { g_string_append(captured_print, text); }
static guint refresh_calls;
static guint missing_agent_attempts;
static const char *authentication_failure;
static gboolean flatpak_installed = TRUE;
static guint flatpak_calls;
static gboolean available(const char *path, GFileTest flags)
{
    if (g_str_equal(path, "/usr/bin/apt")) return apt_installed;
    if (g_str_equal(path, "/usr/bin/flatpak")) return flatpak_installed;
    if (g_str_equal(path, "/usr/bin/snap") ||
        g_str_equal(path, "/usr/bin/pkexec") || g_str_has_suffix(path, "/luxurystore-apt")) return TRUE;
    return g_file_test(path, flags);
}
static GSubprocess *fake_command(GSubprocessLauncher *launcher, const char *const *argv, GError **error)
{
    const char *text = NULL;
    g_assert_cmpstr(argv[0], ==, "/usr/bin/timeout");
    const char *tool = argv[3];
    if (g_str_equal(tool, "/usr/bin/apt") && g_atomic_int_get(&slow_apt_probe)) {
        g_atomic_int_set(&slow_apt_entered, 1);
        while (g_atomic_int_get(&slow_apt_probe)) g_usleep(1000);
    }
    if (g_str_equal(tool, "/usr/bin/apt")) apt_calls++;
    if (g_str_equal(tool, "/usr/bin/apt") && fail_apt_probe) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "APT fixture failure"); return NULL;
    }
    if (g_str_equal(tool, "/usr/bin/apt"))
        text = empty_apt ? "" : malformed_apt ? "Listing...\nunrecognized-package-output\n" : "Listing...\nalpha/stable 2 amd64 [upgradable from: 1]\n";
    else if (g_str_equal(tool, "/usr/bin/flatpak")) {
        flatpak_calls++;
        if ((fail_flatpak_user && g_str_equal(argv[5], "--user")) ||
            (fail_flatpak_system && g_str_equal(argv[5], "--system"))) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Flatpak fixture failure"); return NULL;
        }
        text = g_str_equal(argv[4], "remotes") ? "flathub\thttps://dl.flathub.org/repo/\n" :
            "app/org.test.App/x86_64/stable\tTest app\t2\napp/org.test.Short/x86_64/stable\n";
    }
    else if (g_str_equal(tool, "/usr/bin/snap")) {
        if (g_str_equal(argv[5], "--list") && fail_snap_list) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Snap fixture failure"); return NULL;
        }
        if (g_str_equal(argv[5], "--time") && snap_time_mode == 1) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "simulated Snap policy failure");
            return NULL;
        }
        text = g_str_equal(argv[5], "--time")
            ? (snap_time_mode == 2 ? "unrecognized output\n" :
               snap_time_mode == 3 ? "timer: 00:00~24:00/4\nhold: 2027-01-01T00:00:00Z\n" :
               snap_time_mode == 4 ? "timer: 00:00~24:00/4\nlast: today\n" :
               snap_time_mode == 5 ? "hold: eventually\n" :
               snap_time_mode == 6 ? "timer: 00:00~24:00/4\nlast: today at 07:47 UTC\nnext: today at 12:13 UTC\n" :
               "hold: forever\n")
            : "Name Version Rev Publisher Notes\ntest-snap 2 23 test -\n";
    }
    else if (g_str_equal(tool, "/usr/bin/pkexec")) {
        refresh_calls++;
        if (missing_agent_attempts || authentication_failure) {
            const char *message = authentication_failure;
            if (missing_agent_attempts) {
                missing_agent_attempts--;
                message = "Error executing command as another user: No authentication agent found.\n";
            }
            const char *fixture[] = { "/bin/sh", "-c", "printf '%s' \"$1\" >&2; exit 127", "fixture", message, NULL };
            return g_subprocess_launcher_spawnv(launcher, fixture, error);
        }
        if (fail_refresh) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "simulated refresh failure");
            return NULL;
        }
        text = "";
    }
    if (!text) return g_subprocess_launcher_spawnv(launcher, argv, error);
    const char *fixture[] = { "/usr/bin/printf", "%s", text, NULL };
    return g_subprocess_launcher_spawnv(launcher, fixture, error);
}
#define g_file_test available
#define g_subprocess_launcher_spawnv fake_command
#define LUXUP_AGENT_RETRIES 3
#define LUXUP_AGENT_RETRY_MS 150
#define LUXUP_COMMAND_TIMEOUT "1s"
#include "../check.c"
#undef g_file_test
#undef g_subprocess_launcher_spawnv
#include "../tray.c"
#define main luxup_main
#include "../main.c"
#undef main

static void drain(void)
{
    for (guint i = 0; i < 40; i++) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
}

static gpointer cancel_later(gpointer data)
{
    g_usleep(100000);
    g_cancellable_cancel(data);
    return NULL;
}

static unsigned long tray_pixel(LuxUpTray *tray, int x, int y)
{
    XSync(tray->dpy, False);
    XImage *image = XGetImage(tray->dpy, tray->window, x, y, 1, 1, AllPlanes, ZPixmap);
    g_assert_nonnull(image);
    unsigned long pixel = XGetPixel(image, 0, 0);
    XDestroyImage(image);
    return pixel;
}

static void assert_rgb(unsigned long pixel, const GdkRGBA *color)
{
    int expected[] = {(int)(color->red*255+.5), (int)(color->green*255+.5),
                      (int)(color->blue*255+.5)};
    for (int i = 0; i < 3; i++) {
        int actual = (pixel >> (16-8*i)) & 255;
        g_assert_cmpint(ABS(actual-expected[i]), <=, 2);
    }
}

static void test_backend(void)
{
    LuxUpResult *result = luxup_check(FALSE, NULL);
    g_assert_false(result->check_failed);
    g_assert_cmpint(result->apt_refreshed_at, ==, 0);
    g_assert_cmpuint(luxup_count(result, LUXUP_SOURCE_APT), ==, 1);
    g_assert_cmpuint(luxup_count(result, LUXUP_SOURCE_FLATPAK), ==, 4);
    guint user = 0, system = 0;
    for (guint i = 0; i < result->packages->len; i++) {
        LuxUpPackage *package = g_ptr_array_index(result->packages, i);
        if (package->source != LUXUP_SOURCE_FLATPAK) continue;
        g_assert_true(g_str_has_prefix(package->ref, "app/"));
        if (g_str_equal(package->scope, "user")) user++;
        if (g_str_equal(package->scope, "system")) system++;
    }
    g_assert_cmpuint(user, ==, 2);
    g_assert_cmpuint(system, ==, 2);
    g_assert_cmpuint(luxup_count(result, LUXUP_SOURCE_SNAP), ==, 1);
    g_assert_true(result->snap_auto_refresh_held);
    g_assert_true(result->snap_hold_known);
    luxup_result_free(result);
    guint apt_before = apt_calls, refresh_before = refresh_calls;
    apt_installed = FALSE;
    result = luxup_check(TRUE, NULL);
    g_assert_false(result->apt_available);
    g_assert_false(result->check_failed);
    g_assert_cmpuint(apt_calls, ==, apt_before);
    g_assert_cmpuint(refresh_calls, ==, refresh_before);
    g_assert_cmpuint(luxup_count(result, LUXUP_SOURCE_FLATPAK), ==, 4);
    g_assert_cmpuint(luxup_count(result, LUXUP_SOURCE_SNAP), ==, 1);
    luxup_result_free(result);
    apt_installed = TRUE;
    for (snap_time_mode = 1; snap_time_mode <= 6; snap_time_mode++) {
        result = luxup_check(FALSE, NULL);
        g_assert_false(result->check_failed);
        g_assert_cmpuint(luxup_count(result, LUXUP_SOURCE_SNAP), ==, 1);
        g_assert_cmpint(result->snap_hold_known, ==, snap_time_mode == 3 || snap_time_mode == 6);
        g_assert_false(result->snap_auto_refresh_held);
        luxup_result_free(result);
    }
    snap_time_mode = 0;
    fail_apt_probe = fail_flatpak_user = fail_flatpak_system = fail_snap_list = TRUE;
    result = luxup_check(FALSE, NULL);
    g_assert_true(result->check_failed);
    g_assert_nonnull(strstr(result->apt_check_error, "APT fixture failure"));
    g_assert_nonnull(strstr(result->flatpak_user_error, "Flatpak user"));
    g_assert_nonnull(strstr(result->flatpak_system_error, "Flatpak system"));
    g_assert_nonnull(strstr(result->snap_check_error, "Snap fixture failure"));
    luxup_result_free(result);
    fail_apt_probe = fail_flatpak_user = fail_flatpak_system = fail_snap_list = FALSE;
    captured_print = g_string_new(NULL);
    GPrintFunc previous_print = g_set_print_handler(capture_print);
    snap_time_mode = 1;
    g_assert_cmpint(print_check(FALSE), ==, 0);
    g_assert_nonnull(strstr(captured_print->str, "Snap auto-refresh\tunknown\n"));
    g_string_truncate(captured_print, 0);
    snap_time_mode = 3;
    g_assert_cmpint(print_check(FALSE), ==, 0);
    g_assert_nonnull(strstr(captured_print->str, "Snap auto-refresh\tnot held indefinitely\n"));
    g_string_truncate(captured_print, 0);
    snap_time_mode = 0;
    g_assert_cmpint(print_check(FALSE), ==, 0);
    g_assert_nonnull(strstr(captured_print->str, "Snap auto-refresh\theld indefinitely\n"));
    g_set_print_handler(previous_print);
    g_string_free(captured_print, TRUE); captured_print = NULL;
    guint before = flatpak_calls;
    flatpak_installed = FALSE;
    result = luxup_check(FALSE, NULL);
    g_assert_false(result->check_failed);
    g_assert_cmpuint(luxup_count(result, LUXUP_SOURCE_APT), ==, 1);
    g_assert_cmpuint(luxup_count(result, LUXUP_SOURCE_FLATPAK), ==, 0);
    g_assert_cmpuint(flatpak_calls, ==, before);
    luxup_result_free(result);
    flatpak_installed = TRUE;
    fail_refresh = TRUE;
    result = luxup_check(TRUE, NULL);
    g_assert_true(result->check_failed);
    g_assert_nonnull(result->apt_refresh_error);
    luxup_result_free(result);
    fail_refresh = FALSE;
    malformed_apt = TRUE;
    result = luxup_check(FALSE, NULL);
    g_assert_true(result->check_failed);
    g_assert_nonnull(result->apt_check_error);
    luxup_result_free(result);
    malformed_apt = FALSE;
    empty_apt = TRUE;
    result = luxup_check(FALSE, NULL);
    g_assert_true(result->check_failed);
    g_assert_nonnull(result->apt_check_error);
    luxup_result_free(result);
    empty_apt = FALSE;
    result = luxup_check(TRUE, NULL);
    g_assert_true(luxup_apt_lists_fresh(result, g_get_real_time()/G_USEC_PER_SEC));
    g_assert_false(luxup_apt_lists_fresh(result, result->apt_refreshed_at - 1));
    g_assert_false(luxup_apt_lists_fresh(result, result->apt_refreshed_at + LUXUP_APT_FRESH_SECONDS + 1));
    luxup_result_free(result);
    GCancellable *cancel = g_cancellable_new();
    GThread *thread = g_thread_new("cancel-check", cancel_later, cancel);
    GError *error = NULL;
    const char *args[] = { "/bin/sh", "-c", "exec sleep 30", NULL };
    gint64 start = g_get_monotonic_time();
    char *output = run_command(args, NULL, cancel, &error);
    g_assert_null(output);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_cmpint(g_get_monotonic_time() - start, <, 6000000);
    g_clear_error(&error);
    g_thread_join(thread);
    g_object_unref(cancel);
    cancel = g_cancellable_new();
    thread = g_thread_new("cancel-closed-pipes", cancel_later, cancel);
    const char *closed_pipes[] = {"/bin/sh", "-c", "exec 1>&- 2>&-; exec sleep 30", NULL};
    start = g_get_monotonic_time();
    output = run_command(closed_pipes, NULL, cancel, &error);
    g_assert_null(output);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_cmpint(g_get_monotonic_time() - start, <, 6000000);
    g_clear_error(&error);
    g_thread_join(thread);
    g_object_unref(cancel);
    const char *large_stdout[] = {"/bin/sh", "-c", "yes x | head -c 9000000", NULL};
    output = run_command(large_stdout, NULL, NULL, &error);
    g_assert_null(output);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error(&error);
    const char *both_streams[] = {"/bin/sh", "-c", "(yes x | head -c 200000) & yes y | head -c 200000 >&2; wait", NULL};
    output = run_command(both_streams, NULL, NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(output);
    g_assert_cmpuint(strlen(output), ==, 200000);
    g_free(output);
    const char *large_stderr[] = {"/bin/sh", "-c", "yes x | head -c 300000 >&2", NULL};
    output = run_command(large_stderr, NULL, NULL, &error);
    g_assert_null(output);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error(&error);
    const char *binary_output[] = {"/bin/sh", "-c", "printf '\\377'", NULL};
    output = run_command(binary_output, NULL, NULL, &error);
    g_assert_null(output);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    const char *slow[] = {"/bin/sh", "-c", "exec sleep 5", NULL};
    output = run_command(slow, NULL, NULL, &error);
    g_assert_null(output);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    g_clear_error(&error);
}

static void test_service_package_identity(void)
{
    GVariantBuilder state, rows;
    g_variant_builder_init(&state, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_init(&rows, G_VARIANT_TYPE("a(isssss)"));
    g_variant_builder_add(&rows, "(isssss)", LUXUP_SOURCE_FLATPAK, "Test", "1", "2",
                          "user", "app/org.test.App/x86_64/stable");
    g_variant_builder_add(&state, "{sv}", "has-result", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&state, "{sv}", "snap-hold-known", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&state, "{sv}", "snap-held", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&state, "{sv}", "flatpak-user-error", g_variant_new_string("Flatpak user: failed"));
    g_variant_builder_add(&state, "{sv}", "snap-check-error", g_variant_new_string("Snap: failed"));
    g_variant_builder_add(&state, "{sv}", "packages-v2", g_variant_builder_end(&rows));
    g_autoptr(GVariant) values = g_variant_ref_sink(g_variant_builder_end(&state));
    service_state(values);
    g_assert_true(luxup.result->snap_hold_known);
    g_assert_true(luxup.result->snap_auto_refresh_held);
    g_assert_cmpstr(luxup.result->flatpak_user_error, ==, "Flatpak user: failed");
    g_assert_cmpstr(luxup.result->snap_check_error, ==, "Snap: failed");
    g_assert_cmpuint(luxup.result->packages->len, ==, 1);
    LuxUpPackage *package = g_ptr_array_index(luxup.result->packages, 0);
    g_assert_cmpstr(package->scope, ==, "user");
    g_assert_cmpstr(package->ref, ==, "app/org.test.App/x86_64/stable");
    luxup_result_free(luxup.result); luxup.result = NULL;

    g_variant_builder_init(&state, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_init(&rows, G_VARIANT_TYPE("a(isss)"));
    g_variant_builder_add(&rows, "(isss)", LUXUP_SOURCE_APT, "legacy", "1", "2");
    g_variant_builder_add(&state, "{sv}", "has-result", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&state, "{sv}", "packages", g_variant_builder_end(&rows));
    g_clear_pointer(&values, g_variant_unref);
    values = g_variant_ref_sink(g_variant_builder_end(&state));
    service_state(values);
    g_assert_false(luxup.result->snap_hold_known);
    g_assert_cmpuint(luxup.result->packages->len, ==, 1);
    package = g_ptr_array_index(luxup.result->packages, 0);
    g_assert_cmpstr(package->name, ==, "legacy");
    g_assert_null(package->scope);
    luxup_result_free(luxup.result); luxup.result = NULL;
}

static void test_tray(void)
{
    Display *manager_display = XOpenDisplay(NULL);
    g_assert_nonnull(manager_display);
    Window root = DefaultRootWindow(manager_display);
    Window manager = XCreateSimpleWindow(manager_display, root, 0, 0, 100, 30, 0, 0, 0);
    XMapWindow(manager_display, manager);
    Atom selection = XInternAtom(manager_display, "_NET_SYSTEM_TRAY_S0", False);
    XSetSelectionOwner(manager_display, selection, manager, CurrentTime);
    XSync(manager_display, False);
    LuxUpTray *tray = luxup_tray_new(NULL, NULL);
    g_assert_nonnull(tray);
    Atom marker = XInternAtom(tray->dpy, "_LUXE_TRAY_EDGE_AWARE", False);
    Atom actual = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *data = NULL;
    g_assert_cmpint(XGetWindowProperty(tray->dpy, tray->window, marker, 0, 1,
        False, XA_CARDINAL, &actual, &format, &count, &remaining, &data), ==, Success);
    g_assert_cmpuint(actual, ==, XA_CARDINAL);
    g_assert_cmpint(format, ==, 32);
    g_assert_cmpuint(count, ==, 1);
    g_assert_cmpuint(((unsigned long *)data)[0], ==, 1);
    XFree(data);
    luxup_tray_set_tooltip(tray, "test tooltip");
    Window first = tray->window;
    XReparentWindow(manager_display, first, manager, 0, 0);
    XMapWindow(manager_display, first);
    XSync(manager_display, False);
    drain();
    g_assert_true(tray_embedded(tray));
    luxup_tray_set_state(tray, LUXUP_TRAY_UPTODATE);
    GdkRGBA background, outline;
    tray_panel_colors(&background, &outline);
    unsigned long edge = tray_pixel(tray, 0, 0);
    unsigned long middle = tray_pixel(tray, 0, 1);
    g_assert_cmpuint(edge & 0xffffff, !=, middle & 0xffffff);
    assert_rgb(edge, &outline);
    assert_rgb(middle, &background);
    assert_rgb(tray_pixel(tray, 0, tray->height-1), &outline);
    if (tray->argb) {
        g_assert_cmpuint(edge >> 24, ==, 255);
        /* A compositor does not blend XEmbed children: stay opaque. */
        Atom compositor = XInternAtom(manager_display, "_NET_WM_CM_S0", False);
        XSetSelectionOwner(manager_display, compositor, manager, CurrentTime);
        XSync(manager_display, False);
        tray_draw(tray);
        g_assert_cmpuint(tray_pixel(tray, 0, 0) >> 24, ==, 255);
        assert_rgb(tray_pixel(tray, 0, 0), &outline);
        assert_rgb(tray_pixel(tray, 0, 1), &background);
        XSetSelectionOwner(manager_display, compositor, None, CurrentTime);
        XSync(manager_display, False);
        tray_draw(tray);
        assert_rgb(tray_pixel(tray, 0, 0), &outline);
    }
    g_autofree char *original_theme = NULL;
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &original_theme, NULL);
    g_object_set(gtk_settings_get_default(), "gtk-theme-name", "Adwaita-dark", NULL);
    drain();
    tray_panel_colors(&background, &outline);
    unsigned long dark_background = tray_pixel(tray, 0, 1);
    assert_rgb(dark_background, &background);
    assert_rgb(tray_pixel(tray, 0, 0), &outline);
    g_object_set(gtk_settings_get_default(), "gtk-theme-name", "Adwaita", NULL);
    drain();
    tray_panel_colors(&background, &outline);
    unsigned long light_background = tray_pixel(tray, 0, 1);
    assert_rgb(light_background, &background);
    assert_rgb(tray_pixel(tray, 0, 0), &outline);
    g_assert_cmpuint(dark_background & 0xffffff, !=, light_background & 0xffffff);
    g_object_set(gtk_settings_get_default(), "gtk-theme-name", original_theme, NULL);
    luxe_style_set_color_scheme(luxe_style_settings(), LUXE_COLOR_SCHEME_FORCE_LIGHT);
    luxe_style_set_color_scheme(luxe_style_settings(), LUXE_COLOR_SCHEME_FORCE_DARK);
    XDestroyWindow(manager_display, manager);
    XSync(manager_display, False);
    luxup_tray_set_state(tray, LUXUP_TRAY_UPDATES); /* stale drawable before queued destruction */
    drain();
    g_assert_cmpuint(tray->window, !=, first);
    g_assert_cmpstr(tray->tooltip, ==, "test tooltip");
    g_assert_false(tray_embedded(tray));
    g_assert_cmpuint(tray->dock_timeout, >, 0);
    luxup_tray_free(tray);
    XCloseDisplay(manager_display);
}

static void test_tray_inset(void)
{
    /* Full-bleed fixture icons make the drawn glyph bounds measurable: the
     * glyph uses the icon grid size that fits a 1 px inset, centred. */
    g_autofree char *root = g_dir_make_tmp("luxup-icon-inset-XXXXXX", NULL);
    g_autofree char *icons = g_build_filename(root, "Inset", "scalable", "status", NULL);
    g_assert_cmpint(g_mkdir_with_parents(icons, 0700), ==, 0);
    g_autofree char *index = g_build_filename(root, "Inset", "index.theme", NULL);
    g_assert_true(g_file_set_contents(index,
        "[Icon Theme]\nName=Inset\nDirectories=scalable/status\n"
        "[scalable/status]\nSize=24\nType=Scalable\nMinSize=1\nMaxSize=512\nContext=Status\n", -1, NULL));
    const char *names[] = {"software-update-available", "emblem-synchronizing-symbolic",
        "dialog-warning", "dialog-question", "emblem-ok-symbolic"};
    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        g_autofree char *file = g_strdup_printf("%s/%s.svg", icons, names[i]);
        g_assert_true(g_file_set_contents(file,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\">"
            "<path fill=\"#ffffff\" d=\"M0 0H24V24H0Z\"/></svg>", -1, NULL));
    }
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    g_auto(GStrv) old_paths = gtk_icon_theme_get_search_path(theme);
    g_autofree char *old_name = gtk_icon_theme_get_theme_name(theme);
    const char *paths[] = {root, NULL};
    gtk_icon_theme_set_search_path(theme, paths);
    g_object_set(gtk_settings_get_default(), "gtk-icon-theme-name", "Inset", NULL);
    const int sizes[] = {16, 24, 28, 32};
    for (guint s = 0; s < G_N_ELEMENTS(sizes); s++)
        for (int state = LUXUP_TRAY_CHECKING; state <= LUXUP_TRAY_UNKNOWN; state++) {
            int size = sizes[s];
            cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
            cairo_t *cr = cairo_create(surface);
            LuxUpTray tray = {.width=size, .height=size, .state=state};
            g_assert_true(tray_draw_icon(cr, &tray));
            cairo_surface_flush(surface);
            unsigned char *data = cairo_image_surface_get_data(surface);
            int stride = cairo_image_surface_get_stride(surface);
            for (int n = 0; n < size; n++) {
                g_assert_cmpuint(((guint32 *)(data))[n] >> 24, ==, 0);
                g_assert_cmpuint(((guint32 *)(data+(size-1)*stride))[n] >> 24, ==, 0);
                g_assert_cmpuint(((guint32 *)(data+n*stride))[0] >> 24, ==, 0);
                g_assert_cmpuint(((guint32 *)(data+n*stride))[size-1] >> 24, ==, 0);
            }
            int glyph = tray_icon_size(size, size);
            int offset = (size - glyph) / 2;
            g_assert_cmpint(glyph, ==, size == 16 ? 14 : 22);
            g_assert_cmpuint(((guint32 *)(data+offset*stride))[offset] >> 24, >, 0);
            g_assert_cmpuint(((guint32 *)(data+(offset+glyph-1)*stride))[offset+glyph-1] >> 24, >, 0);
            if (offset > 1)
                g_assert_cmpuint(((guint32 *)(data+(offset-1)*stride))[offset-1] >> 24, ==, 0);
            g_assert_cmpint(tray.width, ==, size); g_assert_cmpint(tray.height, ==, size);
            cairo_destroy(cr); cairo_surface_destroy(surface);
        }
    g_object_set(gtk_settings_get_default(), "gtk-icon-theme-name", old_name, NULL);
    gtk_icon_theme_set_search_path(theme, (const char *const *)old_paths);
    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        g_autofree char *file = g_strdup_printf("%s/%s.svg", icons, names[i]);
        g_remove(file);
    }
    g_remove(index); g_rmdir(icons);
    g_autofree char *scalable = g_build_filename(root, "Inset", "scalable", NULL);
    g_autofree char *folder = g_build_filename(root, "Inset", NULL);
    g_rmdir(scalable); g_rmdir(folder); g_rmdir(root);
}

static void test_authentication_retry(void)
{
    guint before = refresh_calls;
    missing_agent_attempts = 2;
    g_autofree char *failure = refresh_apt(NULL);
    g_assert_null(failure);
    g_assert_cmpuint(refresh_calls, ==, before + 3);
    missing_agent_attempts = 100;
    before = refresh_calls;
    failure = refresh_apt(NULL);
    g_assert_nonnull(failure);
    g_assert_nonnull(strstr(failure, "No authentication agent found"));
    g_assert_cmpuint(refresh_calls, ==, before + LUXUP_AGENT_RETRIES + 1);
    g_clear_pointer(&failure, g_free);
    missing_agent_attempts = 0;
    const char *errors[] = {
        "Error executing command as another user: Request dismissed",
        "Error executing command as another user: Not authorized",
        "Could not refresh package lists: network unavailable"
    };
    for (guint i = 0; i < G_N_ELEMENTS(errors); i++) {
        authentication_failure = errors[i];
        before = refresh_calls;
        failure = refresh_apt(NULL);
        g_assert_nonnull(failure);
        g_assert_cmpuint(refresh_calls, ==, before + 1);
        g_clear_pointer(&failure, g_free);
    }
    authentication_failure = NULL;
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    before = refresh_calls;
    failure = refresh_apt(cancel);
    g_assert_null(failure);
    g_assert_cmpuint(refresh_calls, ==, before);
    /* Cancel a missing-agent check while it is waiting to retry. */
    g_cancellable_reset(cancel);
    missing_agent_attempts = 100;
    GThread *thread = g_thread_new("cancel-agent-wait", cancel_later, cancel);
    gint64 start = g_get_monotonic_time();
    failure = refresh_apt(cancel);
    g_thread_join(thread);
    g_assert_null(failure);
    g_assert_true(g_cancellable_is_cancelled(cancel));
    g_assert_cmpint(g_get_monotonic_time() - start, <, G_TIME_SPAN_SECOND);
    g_assert_cmpuint(refresh_calls, ==, before + 1);
    missing_agent_attempts = 0;
}

static GDBusMethodInvocation *pending_service_call;
static GDBusMethodInvocation *pending_state_call;
static gboolean hold_service_state;
static guint service_calls_seen;
static gboolean service_last_refresh;
static void slow_service_method(GDBusConnection *connection, const gchar *sender, const gchar *path,
                                const gchar *interface, const gchar *method, GVariant *parameters,
                                GDBusMethodInvocation *invocation, gpointer data)
{
    (void)connection; (void)sender; (void)path; (void)interface; (void)data;
    if (g_str_equal(method, "GetState")) {
        if (hold_service_state) { pending_state_call = invocation; return; }
        GVariantBuilder values;
        g_variant_builder_init(&values, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&values, "{sv}", "has-result", g_variant_new_boolean(TRUE));
        g_variant_builder_add(&values, "{sv}", "apt-available", g_variant_new_boolean(TRUE));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a{sv})", g_variant_builder_end(&values)));
        return;
    }
    g_assert_cmpstr(method, ==, "Check");
    g_variant_get(parameters, "(b)", &service_last_refresh);
    service_calls_seen++;
    pending_service_call = invocation;
}
static const GDBusInterfaceVTable slow_service_vtable = {.method_call = slow_service_method};

static void test_slow_service_call(void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GDBusConnection) bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    g_assert_no_error(error);
    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(
        "<node><interface name='org.luxe.TestSlow'><method name='Check'><arg type='b' direction='in'/></method><method name='GetState'><arg type='a{sv}' direction='out'/></method></interface></node>", &error);
    g_assert_no_error(error);
    guint registration = g_dbus_connection_register_object(bus, "/org/luxe/TestSlow", info->interfaces[0],
                                                           &slow_service_vtable, NULL, NULL, &error);
    g_assert_no_error(error); g_assert_cmpuint(registration, !=, 0);
    const char *name = "org.luxe.TestSlow";
    g_autoptr(GVariant) requested = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", name, 0), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    g_assert_no_error(error); g_assert_nonnull(requested);
    luxup.service = g_dbus_proxy_new_sync(bus, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
        G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS, NULL, name,
        "/org/luxe/TestSlow", "org.luxe.TestSlow", NULL, &error);
    g_assert_no_error(error); g_assert_nonnull(luxup.service);
    luxup.service_attached = TRUE;
    service_calls_seen = 0; pending_service_call = NULL;
    gint64 start = g_get_monotonic_time();
    check_async(FALSE);
    g_assert_cmpint(g_get_monotonic_time() - start, <, 250000);
    g_assert_true(luxup.service_call_pending);
    g_assert_false(gtk_widget_get_sensitive(luxup.check_button));
    check_async(FALSE); /* Repeated activation while pending cannot queue another check. */
    gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!pending_service_call && g_get_monotonic_time() < deadline) g_main_context_iteration(NULL, TRUE);
    g_assert_nonnull(pending_service_call);
    g_assert_cmpuint(service_calls_seen, ==, 1);
    /* The main context handled the request while its reply remains withheld. */
    g_assert_true(luxup.service_call_pending);
    g_dbus_method_invocation_return_value(pending_service_call, NULL);
    pending_service_call = NULL;
    while (luxup.service_call_pending && g_get_monotonic_time() < deadline) g_main_context_iteration(NULL, TRUE);
    g_assert_false(luxup.service_call_pending);
    g_assert_true(gtk_widget_get_sensitive(luxup.check_button));
    LuxUpResult *saved = luxup.result;
    luxup.result = g_new0(LuxUpResult, 1);
    luxup.result->packages = g_ptr_array_new();
    g_signal_connect(luxup.service, "notify::g-name-owner", G_CALLBACK(service_owner_changed), NULL);
    service_calls_seen = 0;
    check_async(FALSE);
    deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!pending_service_call && g_get_monotonic_time() < deadline) g_main_context_iteration(NULL, TRUE);
    g_assert_nonnull(pending_service_call);
    g_autoptr(GVariant) released = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "ReleaseName", g_variant_new("(s)", name), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    g_assert_no_error(error); g_assert_nonnull(released);
    deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!luxup.service_disconnected && g_get_monotonic_time() < deadline) g_main_context_iteration(NULL, TRUE);
    g_assert_true(luxup.service_disconnected);
    g_assert_null(luxup.result);
    g_assert_false(gtk_widget_get_sensitive(luxup.check_button));
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.status)), "disconnected"));
    g_dbus_method_invocation_return_value(pending_service_call, NULL);
    pending_service_call = NULL;
    hold_service_state = TRUE; pending_state_call = NULL;
    g_clear_pointer(&requested, g_variant_unref);
    requested = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", name, 0), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    g_assert_no_error(error); g_assert_nonnull(requested);
    while (!pending_state_call && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    g_assert_nonnull(pending_state_call);
    g_autofree char *owner = g_dbus_proxy_get_name_owner(luxup.service);
    GVariantBuilder newest;
    g_variant_builder_init(&newest, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&newest, "{sv}", "has-result", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&newest, "{sv}", "apt-check-error", g_variant_new_string("newer signal"));
    g_autoptr(GVariant) changed = g_variant_ref_sink(g_variant_new("(@a{sv})", g_variant_builder_end(&newest)));
    service_changed(luxup.service, owner, "Changed", changed, NULL);
    GVariantBuilder stale;
    g_variant_builder_init(&stale, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&stale, "{sv}", "has-result", g_variant_new_boolean(TRUE));
    g_dbus_method_invocation_return_value(pending_state_call, g_variant_new("(@a{sv})", g_variant_builder_end(&stale)));
    pending_state_call = NULL; hold_service_state = FALSE;
    while ((luxup.service_disconnected || luxup.service_connecting) && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    g_assert_false(luxup.service_disconnected);
    g_assert_false(luxup.service_connecting);
    g_assert_nonnull(luxup.result);
    g_assert_cmpstr(luxup.result->apt_check_error, ==, "newer signal");
    g_assert_true(gtk_widget_get_sensitive(luxup.check_button));
    /* An explicit refresh queued during startup must remain a Check request,
     * even when the recovered service reports a check already in progress. */
    luxup.checking = TRUE;
    luxup.service_connecting = TRUE;
    luxup.service_check_queued = luxup.service_refresh_queued = luxup.service_force_queued = TRUE;
    service_last_refresh = FALSE;
    finish_service_connection();
    deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!pending_service_call && g_get_monotonic_time() < deadline) g_main_context_iteration(NULL, TRUE);
    g_assert_nonnull(pending_service_call);
    g_assert_true(service_last_refresh);
    g_dbus_method_invocation_return_value(pending_service_call, NULL);
    pending_service_call = NULL;
    while (luxup.service_call_pending && g_get_monotonic_time() < deadline) g_main_context_iteration(NULL, TRUE);
    g_assert_false(luxup.service_call_pending);
    luxup.checking = FALSE;
    /* Start a local fallback while the initially absent service is watched,
     * then let the service appear before the local worker completes. */
    g_clear_pointer(&released, g_variant_unref);
    released = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "ReleaseName", g_variant_new("(s)", name), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    g_assert_no_error(error); g_assert_nonnull(released);
    deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!luxup.service_disconnected && g_get_monotonic_time() < deadline) g_main_context_iteration(NULL, TRUE);
    g_assert_true(luxup.service_disconnected);
    luxup.service_attached = FALSE; luxup.service_disconnected = FALSE;
    g_atomic_int_set(&slow_apt_entered, 0); g_atomic_int_set(&slow_apt_probe, 1);
    check_async(FALSE);
    deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!g_atomic_int_get(&slow_apt_entered) && g_get_monotonic_time() < deadline) g_usleep(1000);
    g_assert_true(g_atomic_int_get(&slow_apt_entered));
    g_clear_pointer(&requested, g_variant_unref);
    requested = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", name, 0), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    g_assert_no_error(error); g_assert_nonnull(requested);
    while ((!luxup.service_attached || luxup.service_connecting) && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    g_assert_true(luxup.service_attached); g_assert_false(luxup.service_connecting);
    g_assert_nonnull(luxup.result);
    g_free(luxup.result->apt_check_error);
    luxup.result->apt_check_error = g_strdup("service result survives");
    g_atomic_int_set(&slow_apt_probe, 0);
    gint64 settle = g_get_monotonic_time() + 300000;
    while (g_get_monotonic_time() < settle) { while (g_main_context_iteration(NULL, FALSE)); g_usleep(1000); }
    g_assert_cmpstr(luxup.result->apt_check_error, ==, "service result survives");
    luxup_result_free(luxup.result); luxup.result = saved;
    g_signal_handlers_disconnect_by_func(luxup.service, service_owner_changed, NULL);
    g_clear_object(&luxup.service_calls);
    g_clear_object(&luxup.service);
    luxup.service_attached = FALSE;
    g_dbus_connection_unregister_object(bus, registration);
}

static void test_ui(void)
{
    luxup.application = GTK_APPLICATION(luxe_application_new("org.luxe.LuxUp.Test", G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(luxup.application), NULL, NULL));
    startup(luxup.application, NULL);
    luxup.background = TRUE;
    luxup.refresh_on_launch = FALSE;
    activate(luxup.application, NULL);
    g_assert_null(luxup.window);
    g_assert_null(gtk_application_get_windows(luxup.application));
    gint64 background_deadline = g_get_monotonic_time() + 5000000;
    while (luxup.checking && g_get_monotonic_time() < background_deadline) drain();
    g_assert_false(luxup.checking);
    g_assert_cmpuint(result_count(luxup.result), ==, 6);
    g_assert_null(luxup.window);
    tray_activated(luxup.tray, 1, NULL);
    g_assert_true(GTK_IS_WINDOW(luxup.window));
    LuxUpResult *saved = luxup.result;
    LuxUpResult empty = {.packages = g_ptr_array_new(), .apt_available = TRUE};
    luxup.result = &empty;
    update_ui();
    empty.snap_available = TRUE;
    update_ui();
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.detail)), "could not be determined"));
    empty.snap_hold_known = TRUE;
    update_ui();
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.detail)), "not held indefinitely"));
    set_tray_state();
    if (luxup.tray) g_assert_cmpint(luxup.tray->state, ==, LUXUP_TRAY_UNKNOWN);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.status)), "freshness is unknown or stale"));
    empty.apt_refreshed_at = g_get_real_time()/G_USEC_PER_SEC;
    update_ui();
    set_tray_state();
    if (luxup.tray) g_assert_cmpint(luxup.tray->state, ==, LUXUP_TRAY_UPTODATE);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.status)), "recently refreshed"));
    empty.apt_available = FALSE;
    update_ui(); set_tray_state();
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.status)), "APT is unavailable"));
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.detail)), "other system package managers are not checked"));
    if (luxup.tray) g_assert_cmpint(luxup.tray->state, ==, LUXUP_TRAY_UNKNOWN);
    empty.apt_available = TRUE;
    LuxUpPackage partial = {.source=LUXUP_SOURCE_FLATPAK, .name="Available fixture",
                            .installed="", .available="2", .scope="user", .ref="app/test"};
    g_ptr_array_add(empty.packages, &partial);
    empty.check_failed = TRUE;
    empty.apt_check_error = "APT listing incomplete";
    empty.flatpak_user_error = "Flatpak user: failed";
    update_ui(); set_tray_state();
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.status)), "some checks failed"));
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.detail)), "APT listing incomplete"));
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(luxup.detail)), "Flatpak user: failed"));
    g_assert_true(gtk_widget_get_sensitive(luxup.review_button));
    g_assert_nonnull(gtk_widget_get_first_child(luxup.list));
    if (luxup.tray) g_assert_cmpint(luxup.tray->state, ==, LUXUP_TRAY_UPDATES);
    g_ptr_array_unref(empty.packages);
    luxup.result = saved;
    update_ui();
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(luxup.application)), ==, 1);
    g_assert_true(gtk_widget_get_sensitive(luxup.review_button));
    g_assert_nonnull(gtk_widget_get_first_child(luxup.list));
    open_window();
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(luxup.application)), ==, 1);
    g_assert_nonnull(g_action_map_lookup_action(G_ACTION_MAP(luxup.application), "quit"));
    open_window();
    test_slow_service_call();
    check_async(FALSE);
    g_assert_true(gtk_spinner_get_spinning(GTK_SPINNER(luxup.spinner)));
    g_assert_false(gtk_widget_get_sensitive(luxup.review_button));
    gint64 deadline = g_get_monotonic_time() + 5000000;
    while (luxup.checking && g_get_monotonic_time() < deadline) drain();
    g_assert_false(luxup.checking);
    g_assert_cmpuint(result_count(luxup.result), ==, 6);
    g_assert_true(gtk_widget_get_sensitive(luxup.review_button));
    g_assert_false(gtk_spinner_get_spinning(GTK_SPINNER(luxup.spinner)));
    guint before = refresh_calls;
    /* Every user-facing check must request fresh lists, not cached status. */
    for (guint action = 0; action < 3; action++) {
        if (action == 0) g_signal_emit_by_name(luxup.check_button, "clicked");
        else if (action == 1) g_action_group_activate_action(G_ACTION_GROUP(luxup.window), "check", NULL);
        else tray_activated(luxup.tray, 2, NULL);
        deadline = g_get_monotonic_time() + 5000000;
        while (luxup.checking && g_get_monotonic_time() < deadline) drain();
        g_assert_false(luxup.checking);
        g_assert_cmpuint(refresh_calls, ==, before + action + 1);
    }
    before = refresh_calls;
    check_async(FALSE);
    luxup.refresh_pending = TRUE;
    g_cancellable_cancel(luxup.cancellable);
    deadline = g_get_monotonic_time() + 5000000;
    while ((luxup.checking || luxup.refresh_pending) && g_get_monotonic_time() < deadline) drain();
    g_assert_false(luxup.checking);
    g_assert_cmpuint(refresh_calls, ==, before + 1);
    show_about(luxup.window);
    drain();
    GtkWindow *dialog = luxe_window_get_visible_dialog(GTK_WINDOW(luxup.window));
    g_assert_true(GTK_IS_ABOUT_DIALOG(dialog));
    g_assert_cmpstr(gtk_about_dialog_get_version(GTK_ABOUT_DIALOG(dialog)), ==, VERSION);
    luxe_dialog_close(dialog);
    check_async(FALSE);
    luxup.shutting_down = TRUE;
    g_cancellable_cancel(luxup.cancellable);
    while (luxup.checking) g_main_context_iteration(NULL, TRUE);
    gtk_window_destroy(luxup.window);
    luxup.window = NULL;
    drain();
    luxup_result_free(luxup.result);
    luxup_tray_free(luxup.tray);
    g_clear_object(&luxup.application);
    memset(&luxup, 0, sizeof luxup);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    luxe_ui_init();
    g_test_add_func("/luxup/backend", test_backend);
    g_test_add_func("/luxup/authentication-retry", test_authentication_retry);
    g_test_add_func("/luxup/tray", test_tray);
    g_test_add_func("/luxup/service-package-identity", test_service_package_identity);
    g_test_add_func("/luxup/tray-inset", test_tray_inset);
    g_test_add_func("/luxup/ui", test_ui);
    return g_test_run();
}
