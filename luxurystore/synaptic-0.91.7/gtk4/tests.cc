// SPDX-License-Identifier: GPL-2.0-or-later
#include <gio/gio.h>
// Keep automatic source discovery independent of the host's installed stores.
// Only the explicit legacy network test may execute real store commands.
static bool real_store_commands = false;
static gboolean test_file_available(const char *path, GFileTest flags) {
    if (!real_store_commands) {
        if (g_str_equal(path, "/usr/bin/flatpak"))
            return g_getenv("LUXURYSTORE_TEST_FLATPAK") != nullptr;
        if (g_str_equal(path, "/usr/bin/snap")) return false;
    }
    return g_file_test(path, flags);
}
static GSubprocess *test_store_command(GSubprocessLauncher *launcher,
                                     const char *const *argv, GError **error) {
    if (!real_store_commands && g_str_equal(argv[0], "/usr/bin/flatpak")) {
        g_assert_nonnull(g_getenv("LUXURYSTORE_TEST_FLATPAK"));
        const char *output = nullptr;
        if (g_str_equal(argv[1], "remotes"))
            output = "flathub\thttps://dl.flathub.org/repo/\n";
        else if (g_str_equal(argv[1], "remote-ls"))
            output = g_str_equal(argv[4], "--updates") ? "app/org.test.Player/x86_64/stable\n" :
                "app/org.test.Player/x86_64/stable\tTest player\t2\tFixture player\n";
        else if (g_str_equal(argv[1], "list"))
            output = g_str_equal(argv[2], "--user") ? "" :
                "app/org.test.Player/x86_64/stable\tTest player\t1\tflathub\n";
        g_assert_nonnull(output); // No remote-add or other mutation is permitted.
        const char *fixture[] = {"/usr/bin/printf", "%s", output, nullptr};
        return g_subprocess_launcher_spawnv(launcher, fixture, error);
    }
    return g_subprocess_launcher_spawnv(launcher, argv, error);
}
#define g_file_test test_file_available
#define g_subprocess_launcher_spawnv test_store_command
#define main luxurystore_application_main
#include "main.cc"
#undef main
#undef g_file_test
#undef g_subprocess_launcher_spawnv
#include <cassert>
#undef assert
#define assert(expression) g_assert((expression))
#include <functional>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <glib/gstdio.h>
#include "luxe-gtk-ui.h"
static void drain(int milliseconds) {
    auto end = g_get_monotonic_time() + milliseconds * 1000;
    while (g_get_monotonic_time() < end) {
        while (g_main_context_iteration(nullptr, false)) {}
        g_usleep(1000);
    }
}
static void capture(GtkWindow *window, const char *path) {
    auto paintable = gtk_widget_paintable_new(GTK_WIDGET(window));
    auto snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), gtk_widget_get_width(GTK_WIDGET(window)), gtk_widget_get_height(GTK_WIDGET(window)));
    auto node = gtk_snapshot_free_to_node(snapshot);
    assert(node);
    auto renderer = gtk_native_get_renderer(GTK_NATIVE(window));
    auto texture = gsk_renderer_render_texture(renderer, node, nullptr);
    assert(gdk_texture_save_to_png(texture, path));
    g_object_unref(texture); gsk_render_node_unref(node); g_object_unref(paintable);
}
static int marked_cells(GtkWidget *widget) {
    int count = gtk_widget_has_css_class(widget, "luxury-mark-install") ||
                gtk_widget_has_css_class(widget, "luxury-mark-remove");
    for (auto child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
        count += marked_cells(child);
    return count;
}
int main(int argc, char **argv) {
    if (getuid() == 0) {
        g_printerr("luxurystore tests refuse to run as root; helper mutation paths are not test fixtures.\n");
        return 77;
    }
    GError *temp_error = nullptr;
    bool output_limited = false;
    try { command({"/usr/bin/head", "-c", "70000", "/dev/zero"}); }
    catch (const std::exception &e) { output_limited = std::string(e.what()).find("output limit") != std::string::npos; }
    assert(output_limited);
    bool timed_out = false;
    try { command({"/bin/sh", "-c", "sleep 5 & wait"}); }
    catch (const std::exception &e) { timed_out = std::string(e.what()).find("time limit") != std::string::npos; }
    assert(timed_out);
    GCancellable *command_cancel = g_cancellable_new();
    std::thread cancel_thread([command_cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        g_cancellable_cancel(command_cancel);
    });
    bool cancelled = false;
    gint64 cancel_started = g_get_monotonic_time();
    try { command({"/bin/sh", "-c", "sleep 5 & wait"}, false, command_cancel); }
    catch (const std::exception &e) { cancelled = std::string(e.what()).find("cancelled") != std::string::npos; }
    cancel_thread.join();
    assert(cancelled && g_get_monotonic_time() - cancel_started < 2 * G_USEC_PER_SEC);
    g_object_unref(command_cancel);
    gchar *temp_dir = g_dir_make_tmp("luxurystore-test-XXXXXX", &temp_error);
    assert(temp_dir && !temp_error);
    std::string temp_path = std::string(temp_dir) + "/repository.list";
    std::string target_path = std::string(temp_dir) + "/target";
    assert(g_file_set_contents(target_path.c_str(), "keep\n", -1, &temp_error));
    assert(symlink(target_path.c_str(), temp_path.c_str()) == 0);
    write_repository_atomic(temp_path, "replacement\n");
    gchar *target_contents = nullptr;
    gsize target_size = 0;
    assert(g_file_get_contents(target_path.c_str(), &target_contents, &target_size, &temp_error));
    assert(std::string(target_contents, target_size) == "keep\n");
    g_free(target_contents);
    assert(g_file_get_contents(temp_path.c_str(), &target_contents, &target_size, &temp_error));
    assert(std::string(target_contents, target_size) == "replacement\n");
    g_free(target_contents);
    g_unlink(temp_path.c_str());
    g_rmdir(temp_dir);
    g_free(temp_dir);

    const std::string list = "# comment\ndeb [signed-by=/key] https://example.test stable main\n";
    auto disabled_list = repository_rewrite(list, false, false);
    assert(!repository_enabled_text(disabled_list, false));
    assert(repository_rewrite(disabled_list, false, true) == list);
    const std::string sources = "Types: deb\nURIs: https://example.test\nSuites: stable\nSigned-By: /key\n\n";
    auto disabled_sources = repository_rewrite(sources, true, false);
    assert(disabled_sources.find("Types: deb\nEnabled: no\nURIs:") != std::string::npos);
    assert(!repository_enabled_text(disabled_sources, true));
    assert(repository_enabled_text(repository_rewrite(disabled_sources, true, true), true));
    assert(!repository_by_id("../../sources.list"));
    auto indented = repository_rewrite("  deb [signed-by=/key] https://example.test stable main\n", false, false);
    assert(indented.find("# luxurystore-disabled") != std::string::npos);
    assert(repository_rewrite(indented, false, true).find("deb [signed-by=/key]") != std::string::npos);
    assert(!repository_enabled_text("# deb https://example.test stable main\n", false));
    g_set_prgname("luxurystore-tests");
    gtk_init();
    luxe_ui_init();
    auto application = luxe_application_new("org.luxe.LuxuryStore.Test", G_APPLICATION_NON_UNIQUE);
    GError *error = nullptr;
    assert(g_application_register(G_APPLICATION(application), nullptr, &error));
    if (g_getenv("LUXURYSTORE_TEST_FLATPAK")) {
        g_signal_connect(application, "activate", G_CALLBACK(activate), nullptr);
        auto deliver_handoff = [&] {
            const char *argv[] = {"luxurystore", "--mark-updates", nullptr};
            auto arguments = g_variant_new_bytestring_array(argv, -1);
            auto line = G_APPLICATION_COMMAND_LINE(g_object_new(G_TYPE_APPLICATION_COMMAND_LINE,
                "arguments", arguments, nullptr));
            assert(command_line(G_APPLICATION(application), line, nullptr) == 0);
            g_object_unref(line);
        };
        app.want_mark_updates = true; // Cold LuxUp hand-off, before APT load.
        open_window(application, nullptr);
        assert(gtk_check_button_get_active(app.sources[0]));
        assert(gtk_check_button_get_active(app.sources[1]));
        assert(!gtk_check_button_get_active(app.sources[2]));
        for (int i = 0; (app.busy || app.store_state.jobs) && i < 600; ++i) drain(50);
        assert(!app.busy && !app.store_state.jobs);
        auto count_flatpaks = [] {
            guint count = 0;
            for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(app.filtered)); ++i) {
                auto object = g_list_model_get_item(G_LIST_MODEL(app.filtered), i);
                if (row(object)->source() == 1) {
                    ++count;
                    assert(row(object)->store->upgradable);
                    assert(row(object)->store->installed == "1");
                }
                g_object_unref(object);
            }
            return count;
        };
        assert(count_flatpaks() == 1);
        gtk_check_button_set_active(app.sources[1], false);
        assert(count_flatpaks() == 0);
        gtk_check_button_set_active(app.sources[1], true);
        for (int i = 0; app.store_state.jobs && i < 600; ++i) drain(50);
        assert(!app.store_state.jobs && count_flatpaks() == 1);
        auto wait_handoff = [] {
            for (int i = 0; (app.busy || app.store_state.jobs || app.want_mark_updates) && i < 600; ++i) drain(50);
            assert(!app.busy && !app.store_state.jobs && !app.want_mark_updates);
            assert(app.store_state.auto_mark);
            assert(app.store_state.requests.size() == 1);
            assert(app.store_state.requests.begin()->second.second == "Update");
            assert(app.package_state.requests.count("luxe-test-upgradable"));
            assert(plan(*app.package_state.apt).find("Upgrade\tluxe-test-upgradable\t1\t2\n") != std::string::npos);
        };
        wait_handoff();
        // A previously loaded/checked channel used to skip automatic marks.
        g_signal_emit_by_name(app.clear, "clicked"); drain(50);
        assert(app.store_state.requests.empty());
        deliver_handoff();
        wait_handoff();
        // A hand-off while resolving must survive until the task completes.
        g_signal_emit_by_name(app.clear, "clicked"); drain(50);
        mark_many({{"luxe-test-manual", "install"}});
        assert(app.busy && app.package_state.resolving);
        deliver_handoff();
        assert(app.want_mark_updates);
        wait_handoff();
        assert(app.package_state.requests.count("luxe-test-manual")); // Existing review preserved.
        gtk_window_destroy(app.window);
        g_object_unref(application);
        g_print("PASS: installed Flatpak, reversible filtering, cold/warm/busy LuxUp update hand-offs.\n");
        return 0;
    }
    // Exercise the real asynchronous gate callback with harmless child exits.
    auto auth = create_authentication(application);
    auto denied = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, "/bin/false", nullptr);
    assert(denied && !error);
    auth->running = true;
    g_subprocess_wait_check_async(denied, nullptr, authentication_finished, auth);
    g_object_unref(denied);
    for (int i = 0; auth->running && i < 100; ++i) drain(20);
    assert(!auth->running && !app.window && !app.package_state.apt);
    assert(gtk_widget_get_sensitive(auth->retry));
    auto allowed = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, "/bin/true", nullptr);
    assert(allowed && !error);
    auth->running = true;
    g_subprocess_wait_check_async(allowed, nullptr, authentication_finished, auth);
    g_object_unref(allowed);
    for (int i = 0; authentication && i < 100; ++i) drain(20);
    assert(!authentication && app.window);
    for (int i = 0; app.busy && i < 600; ++i) drain(50);
    assert(!app.busy && app.package_state.apt && app.package_state.apt->packagesSize() > 0);
    show_repositories();
    drain(100);
    bool found_repositories = false;
    auto repository_windows = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(repository_windows); ++i) {
        auto window = GTK_WINDOW(g_list_model_get_item(repository_windows, i));
        if (safe(gtk_window_get_title(window)) == "LuxuryStore — Repositories") {
            found_repositories = true;
            gtk_window_destroy(window);
        }
        g_object_unref(window);
    }
    assert(found_repositories);
    auto all = g_list_model_get_n_items(G_LIST_MODEL(app.filtered));
    assert(all > 0);
    // Mark the first ordinary APT row; package names differ between Ubuntu
    // and Debian, so do not make the test depend on one distribution's set.
    std::string marker_name;
    guint marker_index = 0;
    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(app.filtered)); ++i) {
        auto object = g_list_model_get_item(G_LIST_MODEL(app.filtered), i);
        auto candidate = row(object);
        if (candidate && candidate->package) { marker_name = candidate->name; marker_index = i; g_object_unref(object); break; }
        g_object_unref(object);
    }
    assert(!marker_name.empty());
    gtk_single_selection_set_selected(app.selection, marker_index); drain(300);
    assert(marked_cells(GTK_WIDGET(app.window)) == 0);
    g_signal_emit_by_name(app.install, "clicked");
    for (int i = 0; (app.package_state.resolving || app.busy) && i < 600; ++i) drain(50);
    drain(300);
    assert(plan(*app.package_state.apt).find("Install\t" + marker_name + "\t") != std::string::npos);
    assert(marked_cells(GTK_WIDGET(app.window)) >= 3);
    if (argc > 5) capture(app.window, argv[5]);
    g_signal_emit_by_name(app.clear, "clicked"); drain(300);
    assert(plan(*app.package_state.apt).empty());
    assert(marked_cells(GTK_WIDGET(app.window)) == 0);

    assert(gtk_check_button_get_active(app.sources[0]));
    assert(!gtk_check_button_get_active(app.sources[1]));
    assert(!gtk_check_button_get_active(app.sources[2]));
    gtk_check_button_set_active(app.sources[0], false);
    assert(g_list_model_get_n_items(G_LIST_MODEL(app.filtered)) == 0);
    gtk_check_button_set_active(app.sources[0], true);
    assert(g_list_model_get_n_items(G_LIST_MODEL(app.filtered)) == all);
    gtk_editable_set_text(GTK_EDITABLE(app.search), "flatpak"); drain(300);
    auto matches_count = g_list_model_get_n_items(G_LIST_MODEL(app.filtered));
    if (matches_count > 0) {
        assert(matches_count < all);
        for (guint i = 0; i < matches_count; ++i) {
            auto o = g_list_model_get_item(G_LIST_MODEL(app.filtered), i);
            assert(row(o)->search.find("flatpak") != std::string::npos);
            g_object_unref(o);
        }
    }
    gtk_single_selection_set_selected(app.selection, 0);
    assert(gtk_widget_get_sensitive(app.install));
    assert(plan(*app.package_state.apt).empty());
    std::string available_name;
    for (auto package : app.package_state.apt->getPackages()) {
        if (package && package->name()) { available_name = safe(package->name()); break; }
    }
    assert(!available_name.empty());
    g_setenv("LUXURYSTORE_TEST_RESOLVE_DELAY_MS", "700", TRUE);
    mark(available_name, "install");
    assert(app.package_state.resolving);
    struct UiProbe { gint64 started; gint64 elapsed; bool done; } ui_probe{
        g_get_monotonic_time(), 0, false};
    g_timeout_add(40, +[](gpointer data) -> gboolean {
        auto probe = static_cast<UiProbe *>(data);
        update_status();
        selected_changed();
        refilter();
        probe->elapsed = g_get_monotonic_time() - probe->started;
        probe->done = true;
        return G_SOURCE_REMOVE;
    }, &ui_probe);
    while (!ui_probe.done) drain(10);
    assert(ui_probe.elapsed < 300 * G_TIME_SPAN_MILLISECOND);
    g_unsetenv("LUXURYSTORE_TEST_RESOLVE_DELAY_MS");
    // mark_many() resolves on a worker thread; keep GTK callbacks responsive.
    for (int i = 0; (app.package_state.resolving || app.busy) && i < 600; ++i) drain(50);
    auto review = plan(*app.package_state.apt);
    assert(!review.empty() && review.find(available_name) != std::string::npos);
    auto payload = encode_request(app.package_state.requests, review);
    const char *helper = g_getenv("LUXURYSTORE_TEST_HELPER");
    if (!helper || !*helper) helper = "./build/luxurystore-apt";
    char *args[] = {const_cast<char *>(helper), const_cast<char *>("--simulate"), const_cast<char *>(payload.c_str()), nullptr};
    gchar *output = nullptr, *errors = nullptr; int status;
    assert(g_spawn_sync(nullptr, args, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, &output, &errors, &status, &error));
    if (status) g_printerr("%s", errors);
    assert(g_spawn_check_wait_status(status, &error));
    assert(safe(output) == review); g_free(output); g_free(errors);
    std::string invalid_request(120001, 'A');
    args[2] = const_cast<char *>(invalid_request.c_str());
    assert(g_spawn_sync(nullptr, args, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr,
        &output, &errors, &status, &error));
    assert(!g_spawn_check_wait_status(status, &error));
    g_clear_error(&error);
    g_free(output); g_free(errors);
    char *invalid_args[] = {const_cast<char *>(helper), const_cast<char *>("--simulate"),
        const_cast<char *>("%%%not-base64%%%"), nullptr};
    assert(g_spawn_sync(nullptr, invalid_args, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr,
        &output, &errors, &status, &error));
    assert(!g_spawn_check_wait_status(status, &error));
    g_clear_error(&error);
    g_free(output); g_free(errors);
    char *repo_args[] = {const_cast<char *>(helper), const_cast<char *>("--repo-set"),
        const_cast<char *>("mozilla"), const_cast<char *>("off"), nullptr};
    output = errors = nullptr;
    assert(g_spawn_sync(nullptr, repo_args, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr,
        &output, &errors, &status, &error));
    assert(status != 0 && safe(errors).find("Administrator authentication is required") != std::string::npos);
    g_free(output); g_free(errors);
    auto stale = encode_request(app.package_state.requests, review + "changed"); args[2] = const_cast<char *>(stale.c_str());
    assert(g_spawn_sync(nullptr, args, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, &output, &errors, &status, &error));
    assert(status != 0 && safe(errors).find("changed since review") != std::string::npos);
    g_free(output); g_free(errors);
    gtk_check_button_set_active(app.sources[0], false);
    assert(plan(*app.package_state.apt) == review);
    gtk_check_button_set_active(app.sources[0], true);
    gtk_editable_set_text(GTK_EDITABLE(app.search), ""); drain(300);
    gtk_drop_down_set_selected(app.status_filter, 4);
    assert(g_list_model_get_n_items(G_LIST_MODEL(app.filtered)) > 0);
    drain(300);
    if (argc > 1) capture(app.window, argv[1]);
    app.package_state.requests.clear(); app.package_state.apt->restoreState(app.package_state.baseline); update_apt_snapshot(); refresh_rows();
    assert(plan(*app.package_state.apt).empty());
    assert(!gtk_widget_get_sensitive(app.review));
    Requests removals;
    auto removable = app.package_state.apt->getPackage("libgtksourceview-5-0");
    if (removable && removable->installedVersion())
        removals["libgtksourceview-5-0"] = "remove";
    if (!removals.empty()) {
        resolve(*app.package_state.apt, removals);
        auto removal_plan = plan(*app.package_state.apt);
        g_print("Removal simulation:\n%s", removal_plan.c_str());
        assert(removal_plan.find("Remove\tlibgtksourceview-5-0") != std::string::npos);
        auto removal_payload = encode_request(removals, removal_plan);
        args[2] = const_cast<char *>(removal_payload.c_str());
        assert(g_spawn_sync(nullptr, args, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, &output, &errors, &status, &error));
        assert(g_spawn_check_wait_status(status, &error));
        assert(safe(output) == removal_plan); g_free(output); g_free(errors);
    }
    // A pair of shared libraries and a pair of consumers must be resolved
    // together, with the privileged helper producing exactly the same plan.
    int combined_pairs_tested = 0;
    for (const Requests pair : {
        Requests{{"libgtksourceview-5-0", "remove"}, {"libspelling-1-2", "remove"}},
        Requests{{"lucid", "remove"}, {"papers", "remove"}}
    }) {
        bool installed_pair = true;
        for (auto &[name, action] : pair) {
            auto package = app.package_state.apt->getPackage(name);
            if (!package || !package->installedVersion()) installed_pair = false;
        }
        if (!installed_pair) continue;
        ++combined_pairs_tested;
        app.package_state.apt->restoreState(app.package_state.baseline);
        resolve(*app.package_state.apt, pair);
        auto pair_plan = plan(*app.package_state.apt);
        for (auto &[name, action] : pair) assert(pair_plan.find("Remove\t" + name + "\t") != std::string::npos);
        assert(app.package_state.apt->check() && !_error->PendingError());
        auto encoded = encode_request(pair, pair_plan);
        args[2] = const_cast<char *>(encoded.c_str());
        assert(g_spawn_sync(nullptr, args, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, &output, &errors, &status, &error));
        if (status) g_printerr("%s", errors);
        assert(g_spawn_check_wait_status(status, &error));
        assert(safe(output) == pair_plan);
        g_free(output); g_free(errors);
        g_print("Combined removal simulation:\n%s", pair_plan.c_str());
    }
    if (combined_pairs_tested > 0)
        g_print("Combined removal pair coverage: %d\n", combined_pairs_tested);
    app.package_state.apt->restoreState(app.package_state.baseline);
    resolve(*app.package_state.apt, removals);
    app.package_state.requests = removals;
    show_review(); drain(300);
    auto review_windows = gtk_window_get_toplevels();
    for (guint i = g_list_model_get_n_items(review_windows); i > 0; --i) {
        auto window = GTK_WINDOW(g_list_model_get_item(review_windows, i - 1));
        if (window != app.window) { if (argc > 1) capture(window, argv[1]); gtk_window_destroy(window); }
        g_object_unref(window);
    }
    app.package_state.requests.clear(); app.package_state.apt->restoreState(app.package_state.baseline); update_apt_snapshot();
    gtk_drop_down_set_selected(app.status_filter, 0);
    gtk_editable_set_text(GTK_EDITABLE(app.search), "gtk4"); drain(300);
    if (argc > 2) capture(app.window, argv[2]);
    gtk_drop_down_set_selected(app.status_filter, 1);
    auto installed = g_list_model_get_n_items(G_LIST_MODEL(app.filtered));
    assert(installed > 0);
    for (guint i = 0; i < installed; ++i) {
        auto o = g_list_model_get_item(G_LIST_MODEL(app.filtered), i);
        assert(row(o)->package->getFlags() & RPackage::FInstalled); g_object_unref(o);
    }
    gtk_drop_down_set_selected(app.status_filter, 0);
    show_review(); drain(200);
    auto windows = gtk_window_get_toplevels();
    for (guint i = g_list_model_get_n_items(windows); i > 0; --i) {
        auto window = GTK_WINDOW(g_list_model_get_item(windows, i - 1));
        if (window != app.window) gtk_window_destroy(window);
        g_object_unref(window);
    }
    if (g_getenv("LUXURYSTORE_NETWORK_TEST")) {
        real_store_commands = true;
        gtk_editable_set_text(GTK_EDITABLE(app.search), "firefox"); drain(300);
        gtk_check_button_set_active(app.sources[1], true);
        gtk_check_button_set_active(app.sources[2], true);
        for (int i = 0; app.store_state.jobs && i < 3000; ++i) drain(50);
        assert(app.store_state.jobs == 0);
        int counts[3] = {0, 0, 0};
        for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(app.filtered)); ++i) {
            auto o = g_list_model_get_item(G_LIST_MODEL(app.filtered), i); ++counts[row(o)->source()]; g_object_unref(o);
        }
        g_print("Combined Firefox search: APT=%d, Flathub=%d, Snap=%d\n", counts[0], counts[1], counts[2]);
        assert(counts[0] > 0 && counts[1] > 0 && counts[2] > 0);
        gtk_check_button_set_active(app.sources[0], false);
        gtk_check_button_set_active(app.sources[1], false);
        for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(app.filtered)); ++i) {
            auto o = g_list_model_get_item(G_LIST_MODEL(app.filtered), i); assert(row(o)->source() == 2); g_object_unref(o);
        }
        gtk_check_button_set_active(app.sources[0], true);
        gtk_check_button_set_active(app.sources[1], true);
        for (int i = 0; app.store_state.jobs && i < 3000; ++i) drain(50);
        drain(300);
        if (argc > 3) capture(app.window, argv[3]);
    }
    // Exercise VTE completion, error handling, retained marks and close guard
    // using harmless local commands, never a package mutation.
    for (bool fail : {false, true}) {
        StorePackage pending; pending.source = 1; pending.id = "test";
        app.store_state.requests["progress-test"] = {pending, "Install"};
        run_operations({{1, "progress-test", {"/bin/sh", "-c", fail ? "printf 'Test operation failed\\n'; sleep 0.2; exit 1" : "printf 'Test operation completed\\n'; sleep 0.2"}}});
        Transaction *transaction = nullptr;
        auto toplevels = gtk_window_get_toplevels();
        for (guint i = 0; i < g_list_model_get_n_items(toplevels); ++i) {
            auto window = GTK_WINDOW(g_list_model_get_item(toplevels, i));
            auto candidate = static_cast<Transaction *>(g_object_get_data(G_OBJECT(window), "transaction"));
            if (candidate) transaction = candidate;
            g_object_unref(window);
        }
        assert(transaction && transaction->running);
        assert(!gtk_widget_get_sensitive(transaction->close));
        assert(!gtk_widget_has_css_class(transaction->close, "destructive-action"));
        gtk_window_close(transaction->window);
        assert(transaction->running);
        for (int i = 0; (transaction->running || app.busy) && i < 600; ++i) drain(50);
        assert(!transaction->running && !app.busy);
        assert(gtk_widget_get_sensitive(transaction->close));
        assert(!gtk_widget_has_css_class(transaction->close, "destructive-action"));
        assert(safe(gtk_button_get_label(GTK_BUTTON(transaction->close))) == "Close");
        assert((app.store_state.requests.count("progress-test") != 0) == fail);
        if (!fail && argc > 4) capture(transaction->window, argv[4]);
        gtk_window_close(transaction->window);
        app.store_state.requests.clear();
    }
    char *auth_args[] = {const_cast<char *>(helper), const_cast<char *>("--authorize"), nullptr};
    assert(g_spawn_sync(nullptr, auth_args, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, &output, &errors, &status, &error));
    assert(status != 0 && safe(errors).find("authentication is required") != std::string::npos);
    g_free(output); g_free(errors);
    for (auto operation : {"--apply", "--refresh", "--snap-remove"}) {
        char *privileged_args[] = {const_cast<char *>(helper), const_cast<char *>(operation), const_cast<char *>("apt"), nullptr};
        assert(g_spawn_sync(nullptr, privileged_args, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, &output, &errors, &status, &error));
        assert(status != 0 && safe(errors).find("authentication is required") != std::string::npos);
        g_free(output); g_free(errors);
    }
    // The real header action must retain Synaptic's credits as well as the new credit.
    // GtkApplicationWindow hosts the header in an GtkBox top bar, never
    // as a titlebar child, so search the whole window for the About button.
    std::function<GtkWidget *(GtkWidget *)> find_about = [&](GtkWidget *widget) -> GtkWidget * {
        if (GTK_IS_BUTTON(widget) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), "About")) return widget;
        for (auto child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
            if (auto found = find_about(child)) return found;
        return nullptr;
    };
    auto about_button = find_about(GTK_WIDGET(app.window)); assert(about_button);
    g_signal_emit_by_name(about_button, "clicked"); drain(400);
    // GtkAboutDialog is an GtkWindow, not a GtkWindow, so it never appears
    // directly in gtk_window_get_toplevels(); search each toplevel's widget
    // tree for the presented dialog.
    std::function<GtkAboutDialog *(GtkWidget *)> find_dialog = [&](GtkWidget *widget) -> GtkAboutDialog * {
        if (GTK_IS_ABOUT_DIALOG(widget)) return GTK_ABOUT_DIALOG(widget);
        for (auto child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
            if (auto found = find_dialog(child)) return found;
        return nullptr;
    };
    auto about_windows = gtk_window_get_toplevels(); GtkAboutDialog *about = nullptr;
    for (guint i = 0; i < g_list_model_get_n_items(about_windows); ++i) {
        auto window = GTK_WIDGET(g_list_model_get_item(about_windows, i));
        if (auto found = find_dialog(window)) about = GTK_ABOUT_DIALOG(g_object_ref(found));
        g_object_unref(window);
        if (about) break;
    }
    assert(about);
    assert(safe(gtk_about_dialog_get_copyright(about)).find("Michael Vogt") != std::string::npos);
    assert(safe(gtk_about_dialog_get_copyright(about)).find("2026 Jerry Howell") != std::string::npos);
    assert(safe(luxe_about_get_license(about)).find("Alfredo") != std::string::npos);
    assert(safe(luxe_about_get_license(about)).find("Jason Gunthorpe") != std::string::npos);
    assert(gtk_about_dialog_get_documenters(about)[0] != nullptr);
    assert(safe(gtk_about_dialog_get_website(about)).find("github.com/openconstruct/luxeDE") != std::string::npos);
    assert(safe(gtk_about_dialog_get_version(about)) == LUXURYSTORE_VERSION);
    luxe_dialog_close(GTK_WINDOW(about)); g_object_unref(about);
    // Disabled repository entries have no package or store metadata. Exercise
    // their real filtering/details/actions, including an emitted disabled
    // Remove action, without changing a repository or invoking a helper.
    auto repository_row = new Row{nullptr, "Luxe repository fixture",
        "Fixture source description", fold("Luxe repository fixture"), {}, "fixture-disabled-source"};
    auto repository_object = g_object_new(package_row_get_type(), nullptr);
    g_object_set_data_full(G_OBJECT(repository_object), "row", repository_row,
        +[](gpointer p) { delete static_cast<Row *>(p); });
    auto repository_position = g_list_model_get_n_items(G_LIST_MODEL(app.store));
    g_list_store_append(app.store, repository_object);
    g_object_unref(repository_object);
    gtk_editable_set_text(GTK_EDITABLE(app.search), "Luxe repository fixture"); drain(300);
    assert(g_list_model_get_n_items(G_LIST_MODEL(app.filtered)) == 1);
    gtk_single_selection_set_selected(app.selection, 0); drain(100);
    assert(safe(gtk_label_get_text(GTK_LABEL(app.details))).find("This source is disabled") != std::string::npos);
    assert(!gtk_widget_get_sensitive(app.remove));
    g_signal_emit_by_name(app.remove, "clicked");
    assert(app.store_state.requests.empty());
    gtk_drop_down_set_selected(app.status_filter, 1);
    assert(g_list_model_get_n_items(G_LIST_MODEL(app.filtered)) == 0);
    gtk_drop_down_set_selected(app.status_filter, 2);
    assert(g_list_model_get_n_items(G_LIST_MODEL(app.filtered)) == 1);
    gtk_drop_down_set_selected(app.status_filter, 0);
    gtk_editable_set_text(GTK_EDITABLE(app.search), ""); drain(300);
    g_list_store_remove(app.store, repository_position);
    assert(g_list_model_get_n_items(G_LIST_MODEL(app.filtered)) == all);
    gtk_window_destroy(app.window); g_object_unref(application);
    g_print("PASS: APT load, source defaults, empty-source view, search, selection, dependency planning, helper simulation, stale-plan rejection, retained marks, clear marks, repeated shared-dependency removals, authentication gate success/failure, unprivileged helper rejection, and embedded terminal success/failure, immediate 0ad badges in the full list, and About credits.\n");
}
