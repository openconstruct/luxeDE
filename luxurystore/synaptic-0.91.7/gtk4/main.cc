// Copyright (C) 2026 Jerry Howell
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../luxe-about.h"
#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <vte/vte.h>
#include "backend.h"
#include "repositories.h"
#include "stores.h"
#include <mutex>
#include <atomic>
#ifdef __GLIBC__
#include <malloc.h>
#endif

#ifndef LUXURYSTORE_HELPER_PATH
#define LUXURYSTORE_HELPER_PATH "/usr/libexec/luxurystore-apt"
#endif
namespace { constexpr const char kLuxuryStoreHelper[] = LUXURYSTORE_HELPER_PATH; }

// Package state changes do not change list membership. Notify bound cells
// explicitly instead of pretending the entire list was removed and reinserted.
struct PackageRow { GObject parent_instance; };
struct PackageRowClass { GObjectClass parent_class; };
G_DEFINE_TYPE(PackageRow, package_row, G_TYPE_OBJECT)
static guint row_changed_signal;
static void package_row_class_init(PackageRowClass *klass) {
    row_changed_signal = g_signal_new("changed", G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST, 0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
}
static void package_row_init(PackageRow *) {}

struct Row {
    RPackage *package;
    std::string name, summary, search;
    // Only Flatpak/Snap rows need their six-string store metadata. Keeping
    // it inline also paid that cost for every APT package in the catalogue.
    std::unique_ptr<StorePackage> store;
    std::string repository_id;
    int package_flags = 0;
    std::string package_action, installed_version, available_version;
    bool last_match = false;
    int source() const { return store ? store->source : 0; }
};
struct AptState {
    std::unique_ptr<RPackageLister> apt;
    RPackageLister::pkgState baseline;
    Requests requests;
    std::string plan_snapshot;
    bool resolving = false;
};
struct StoreState {
    std::map<std::string, std::pair<StorePackage, std::string>> requests;
    int jobs = 0;
    bool mutation = false;
    unsigned generations[3] = {0, 0, 0};
    bool marks_applied[3] = {false, false, false};
    GCancellable *cancellables[3] = {nullptr, nullptr, nullptr};
    guint search_source = 0;
    std::string query;
    std::vector<std::string> query_tokens;
    bool auto_mark = false;
};
struct App {
    GtkApplication *application;
    GtkWindow *window = nullptr;
    GtkWidget *search, *status, *details, *review, *install, *remove, *clear;
    GtkCheckButton *sources[3];
    GtkDropDown *status_filter;
    GListStore *store = nullptr;
    GtkCustomFilter *filter;
    GtkFilterListModel *filtered;
    GtkSingleSelection *selection;
    AptState package_state;
    StoreState store_state;
    // Query split once per keystroke; matches() scans rows against these
    // tokens instead of building a fresh istringstream per row.
    bool busy = true;
    bool review_open = false;
    // Set by "luxurystore --mark-updates" (LuxUp hand-off).  auto_mark keeps
    // marking upgradable store packages as they finish loading.
    bool want_mark_updates = false;
    std::string pending_repository_install;
    GCancellable *lifetime_cancel = nullptr;
    std::atomic_bool closing = false;
} app;
static std::recursive_mutex apt_mutex;

static std::string fold(const std::string &s) {
    gchar *utf8 = g_utf8_make_valid(s.c_str(), s.size());
    gchar *f = g_utf8_casefold(utf8, -1);
    std::string out(f); g_free(utf8); g_free(f); return out;
}
static void label_text(GtkWidget *label, const std::string &s) {
    gchar *utf8 = g_utf8_make_valid(s.c_str(), s.size());
    gtk_label_set_text(GTK_LABEL(label), utf8); g_free(utf8);
}
static Row *row(gpointer object) {
    return object ? static_cast<Row *>(g_object_get_data(G_OBJECT(object), "row")) : nullptr;
}
static GtkWidget *scroll(GtkWidget *child) {
    auto s = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s), child);
    gtk_widget_set_vexpand(s, true); gtk_widget_set_hexpand(s, true); return s;
}
static void message(const char *title, const std::string &text) {
    auto d = gtk_alert_dialog_new("%s", title);
    gtk_alert_dialog_set_detail(d, text.c_str());
    gtk_alert_dialog_show(d, app.window); g_object_unref(d);
}
static std::string status_for(const Row *r) {
    if (!r->package_action.empty()) return r->package_action;
    int f = r->package_flags;
    if (f & RPackage::FNowBroken) return "Broken";
    if (f & RPackage::FOutdated) return "Upgradable";
    if (f & RPackage::FInstalled) return "Installed";
    if (f & RPackage::FResidualConfig) return "Residual config";
    return "Available";
}
static void retokenize_query() {
    // Tokenize once per keystroke; the per-row filter below reuses this.
    app.store_state.query_tokens.clear();
    std::istringstream words(app.store_state.query);
    std::string word;
    while (words >> word) app.store_state.query_tokens.push_back(word);
}
static std::string store_key(const StorePackage &p);
static gboolean matches(gpointer object, gpointer) {
    auto r = row(object);
    auto match = [&] {
    if (!gtk_check_button_get_active(app.sources[r->source()])) return false;
    for (auto &word : app.store_state.query_tokens) if (r->search.find(word) == std::string::npos) return false;
    if (!r->repository_id.empty()) {
        auto state = gtk_drop_down_get_selected(app.status_filter);
        return state == 0 || state == 2;
    }
    if (!r->package) {
        auto state = gtk_drop_down_get_selected(app.status_filter);
        if (state == 1) return !r->store->installed.empty();
        if (state == 2) return r->store->installed.empty();
        if (state == 3) return r->store->upgradable;
        if (state == 4) return app.store_state.requests.count(std::to_string(r->source()) + r->store->scope + ":" + r->store->id) != 0;
        return true;
    }
    int f = r->package_flags;
    switch (gtk_drop_down_get_selected(app.status_filter)) {
        case 1: return (f & RPackage::FInstalled) != 0;
        case 2: return (f & RPackage::FInstalled) == 0;
        case 3: return (f & RPackage::FOutdated) != 0;
        case 4: return !r->package_action.empty();
        default: return true;
    }
    };
    r->last_match = match();
    return r->last_match;
}
static std::string store_key(const StorePackage &p) { return std::to_string(p.source) + p.scope + ":" + p.id; }
static std::string row_action(Row *r) {
    if (!r->repository_id.empty()) return "";
    if (r->package) return r->package_action;
    auto found = app.store_state.requests.find(store_key(*r->store));
    return found == app.store_state.requests.end() ? "" : found->second.second;
}
static std::string combined_plan() {
    std::string text = app.package_state.plan_snapshot;
    for (auto &[id, change] : app.store_state.requests) {
        auto &[p, action] = change;
        text += action + "\t" + p.id + "\t" + (p.source == 1 ? "Flathub " + p.scope : "Snap Store") + "\n";
    }
    return text;
}
static void load_store(int source, bool force = false);
static void update_status() {
    if (!app.package_state.apt && app.filter == nullptr) return;
    auto n = g_list_model_get_n_items(G_LIST_MODEL(app.filtered));
    auto review = combined_plan();
    auto count = std::count(review.begin(), review.end(), '\n');
    std::string text = n ? std::to_string(n) + " packages" :
        (!gtk_check_button_get_active(app.sources[0]) && !gtk_check_button_get_active(app.sources[1]) && !gtk_check_button_get_active(app.sources[2]) ? "Select a source to browse packages" : "No matching packages");
    text += "  ·  " + std::to_string(count) + " changes marked";
    if (app.store_state.jobs) text += "  ·  Searching stores…";
    else if (gtk_check_button_get_active(app.sources[2]) && app.store_state.query.empty()) text += "  ·  Type to search the Snap Store";
    label_text(app.status, text);
    gtk_widget_set_sensitive(app.review, count > 0 && !app.busy);
    gtk_widget_set_sensitive(app.clear, (!app.package_state.requests.empty() || !app.store_state.requests.empty()) && !app.busy);
}
static void refilter() {
    gtk_filter_changed(GTK_FILTER(app.filter), GTK_FILTER_CHANGE_DIFFERENT);
    update_status();
}
static void selected_changed() {
    auto r = row(gtk_single_selection_get_selected_item(app.selection));
    gtk_widget_set_sensitive(app.install, r && !app.busy);
    gtk_widget_set_sensitive(app.remove, r && !app.busy && r->repository_id.empty() && (r->package ? (r->package_flags & RPackage::FInstalled) : !r->store->installed.empty()));
    if (!r) { label_text(app.details, "Select a package to see its description."); return; }
    if (!r->repository_id.empty()) {
        auto repo = repository_by_id(r->repository_id);
        label_text(app.details, r->name + " · " + (repo ? repo->title : "APT") +
            "\nThis source is disabled. Enable it to refresh APT and review the package before installation.\n\n" + r->summary);
        return;
    }
    if (!r->package) {
        label_text(app.details, r->store->title + "\n" + r->store->id + "\n" + (r->source() == 1 ? "Flathub · " + r->store->scope : "Snap Store") + "\n\n" + r->summary);
        return;
    }
    // Package identity and descriptive metadata are immutable for this row;
    // solver flags and versions are copied into the row snapshot separately.
    auto p = r->package;
    label_text(app.details, r->name + "   ·   APT   ·   " + p->arch() + "\n" +
        safe(p->description()) + "\n\n" + safe(p->homepage()));
}
static void refresh_rows() {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(app.store));
    for (guint i = 0; i < n; ++i) {
        auto object = g_list_model_get_item(G_LIST_MODEL(app.store), i);
        g_signal_emit(object, row_changed_signal, 0);
        g_object_unref(object);
    }
    refilter(); selected_changed();
}
static void update_apt_snapshot() {
    std::lock_guard<std::recursive_mutex> lock(apt_mutex);
    app.package_state.plan_snapshot = app.package_state.apt ? plan(*app.package_state.apt) : "";
    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(app.store)); ++i) {
        auto object = g_list_model_get_item(G_LIST_MODEL(app.store), i);
        auto r = row(object);
        if (r->package) {
            r->package_flags = r->package->getFlags();
            r->package_action = action_for(r->package);
            r->installed_version = safe(r->package->installedVersion());
            r->available_version = safe(r->package->availableVersion());
        }
        g_object_unref(object);
    }
}
struct ResolveWork { Requests changes; Requests previous; std::string error; bool fallback_ok = true; };
static void load();
static void reload_pending_updates() {
    if (app.want_mark_updates && app.package_state.apt && !app.busy && !app.closing) load();
}
static void cancel_store_searches() {
    if (app.store_state.search_source) {
        g_source_remove(app.store_state.search_source);
        app.store_state.search_source = 0;
    }
    for (int source = 1; source < 3; ++source) {
        ++app.store_state.generations[source];
        if (app.store_state.cancellables[source]) g_cancellable_cancel(app.store_state.cancellables[source]);
    }
}
static void begin_close() {
    app.closing = true;
    cancel_store_searches();
    if (app.lifetime_cancel) g_cancellable_cancel(app.lifetime_cancel);
}
// APT dependency resolution runs on a worker thread; the result is marshalled
// back to the UI thread. The unprivileged UI never takes the archive lock;
// the privileged helper holds it across resolve/plan/commit.
static void mark_many(const Requests &changes) {
    std::lock_guard<std::recursive_mutex> lock(apt_mutex);
    if (!app.package_state.apt || app.busy || app.package_state.resolving) return;
    auto work = new ResolveWork{changes, app.package_state.requests, "", true};
    app.package_state.requests = changes;
    app.package_state.resolving = true; app.busy = true;
    update_status(); selected_changed();
    auto task = g_task_new(app.window ? G_OBJECT(app.window) : nullptr, app.lifetime_cancel, +[](GObject *, GAsyncResult *result, gpointer) {
        auto task = G_TASK(result);
        auto work = static_cast<ResolveWork *>(g_task_get_task_data(task));
        GError *error = nullptr;
        bool ok = g_task_propagate_boolean(task, &error);
        if (app.closing) { g_clear_error(&error); return; }
        g_clear_error(&error);
        app.package_state.resolving = false; app.busy = false;
        if (!ok) {
            app.package_state.requests = work->fallback_ok ? work->previous : Requests{};
            message("Cannot mark this change", work->error);
        }
        update_apt_snapshot();
        refresh_rows();
        reload_pending_updates();
    }, nullptr);
    g_task_set_task_data(task, work, +[](gpointer p) { delete static_cast<ResolveWork *>(p); });
    g_task_run_in_thread(task, +[](GTask *task, gpointer, gpointer data, GCancellable *) {
        auto work = static_cast<ResolveWork *>(data);
        std::lock_guard<std::recursive_mutex> lock(apt_mutex);
        if (app.closing) { g_task_return_boolean(task, false); return; }
#ifdef LUXURYSTORE_TESTING
        if (const char *delay = g_getenv("LUXURYSTORE_TEST_RESOLVE_DELAY_MS")) {
            auto milliseconds = g_ascii_strtoull(delay, nullptr, 10);
            g_usleep(MIN(milliseconds, 2000) * 1000);
        }
#endif
        try {
            app.package_state.apt->restoreState(app.package_state.baseline);
            resolve(*app.package_state.apt, work->changes);
            g_task_return_boolean(task, true);
        } catch (const std::exception &e) {
            work->error = e.what();
            try {
                app.package_state.apt->restoreState(app.package_state.baseline);
                resolve(*app.package_state.apt, work->previous);
                work->fallback_ok = true;
            } catch (...) {
                try { app.package_state.apt->restoreState(app.package_state.baseline); } catch (...) {}
                work->fallback_ok = false;
            }
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", e.what());
        }
    });
    g_object_unref(task);
}
static void mark(const std::string &name, const std::string &action) {
    auto changes = app.package_state.requests;
    changes[name] = action;
    mark_many(changes);
}
// LuxUp hand-off: mark every upgradable APT package at once, then enable the
// store channels whose runtimes are installed so their updates get marked too.
static void mark_all_updates() {
    if (app.package_state.resolving) { app.want_mark_updates = true; return; }
    if (app.busy || !app.package_state.apt) return;
    Requests updates = app.package_state.requests;
    for (auto p : app.package_state.apt->getPackages())
        if ((p->getFlags() & RPackage::FOutdated) && !updates.count(safe(p->name())))
            updates[safe(p->name())] = "install";
    mark_many(updates);
    app.store_state.auto_mark = true;
    for (int source = 1; source < 3; ++source) {
        app.store_state.marks_applied[source] = false;
        const char *binary = source == 1 ? "/usr/bin/flatpak" : "/usr/bin/snap";
        if (!g_file_test(binary, G_FILE_TEST_IS_EXECUTABLE)) continue;
        if (gtk_check_button_get_active(app.sources[source])) load_store(source, true);
        else gtk_check_button_set_active(app.sources[source], TRUE);
    }
}
static void load();
static gboolean close_requested(GtkWindow *, gpointer) {
    if (app.busy || app.store_state.mutation) {
        message("Please wait", "A package transaction or remote change is still running.");
        return true;
    }
    if (!combined_plan().empty()) {
        auto d = gtk_alert_dialog_new("Discard marked changes?");
        gtk_alert_dialog_set_detail(d, "These changes have not been applied.");
        const char *buttons[] = {"Keep working", "Discard and close", nullptr};
        gtk_alert_dialog_set_buttons(d, buttons); gtk_alert_dialog_set_cancel_button(d, 0);
        gtk_alert_dialog_choose(d, app.window, nullptr, +[](GObject *o, GAsyncResult *r, gpointer) {
            GError *error = nullptr;
            int answer = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(o), r, &error);
            if (answer == 1) {
                app.package_state.requests.clear(); app.store_state.requests.clear(); app.store_state.auto_mark = false;
                if (app.package_state.apt) app.package_state.apt->restoreState(app.package_state.baseline);
                app.package_state.plan_snapshot.clear();
                begin_close();
                gtk_window_destroy(app.window);
            }
            g_clear_error(&error);
        }, nullptr);
        g_object_unref(d); return true;
    }
    begin_close();
    return false;
}
struct Operation { int source; std::string key; std::vector<std::string> args; };
struct Transaction {
    GtkWindow *window;
    VteTerminal *terminal;
    std::vector<Operation> operations;
    size_t next = 0;
    bool running = false;
    GtkWidget *status = nullptr, *progress = nullptr, *close = nullptr, *spinner = nullptr;
};
static void finish_transaction(Transaction *t, bool success) {
    t->running = false; app.busy = false;
    gtk_spinner_stop(GTK_SPINNER(t->spinner));
    gtk_widget_set_sensitive(t->close, true);
    gtk_widget_remove_css_class(t->close, "destructive-action");
    label_text(t->status, success ? "All changes completed." : "The operation stopped. Check the output below for details.");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(t->progress), double(t->next) / t->operations.size());
    gtk_window_set_title(t->window, success ? "LuxuryStore — Completed · close to continue" : "LuxuryStore — Stopped after an error · close to continue");
    gtk_widget_set_sensitive(gtk_window_get_child(app.window), true);
    if (!success) app.pending_repository_install.clear();
    load();
}
static void next_operation(Transaction *t) {
    if (t->next == t->operations.size()) { finish_transaction(t, true); return; }
    auto &operation = t->operations[t->next];
    const char *source = operation.source == 0 ? "APT" : operation.source == 1 ? "Flathub" :
        operation.source == 2 ? "Snap Store" : "APT repository";
    label_text(t->status, "Operation " + std::to_string(t->next + 1) + " of " +
        std::to_string(t->operations.size()) + " · " + source + " — follow any prompts below");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(t->progress), double(t->next) / t->operations.size());
    std::vector<char *> argv;
    for (auto &arg : operation.args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    t->running = true;
    vte_terminal_spawn_async(t->terminal, VTE_PTY_DEFAULT, nullptr, argv.data(), nullptr, G_SPAWN_DEFAULT,
        nullptr, nullptr, nullptr, -1, nullptr,
        +[](VteTerminal *, GPid, GError *error, gpointer data) {
            auto t = static_cast<Transaction *>(data);
            if (error) { message("Could not start operation", error->message); finish_transaction(t, false); }
        }, t);
}
static std::vector<Operation> operations() {
    std::vector<Operation> result;
    auto apt_plan = app.package_state.plan_snapshot;
    if (!apt_plan.empty()) result.push_back({0, "", {"/usr/bin/pkexec", kLuxuryStoreHelper, "--apply", encode_request(app.package_state.requests, apt_plan)}});
    for (auto &[key, change] : app.store_state.requests) {
        auto &[p, action] = change;
        if (p.source == 1) {
            auto verb = action == "Remove" ? "uninstall" : action == "Update" ? "update" : "install";
            std::vector<std::string> args{"/usr/bin/flatpak", verb, p.scope};
            if (action == "Install") args.push_back("flathub");
            args.push_back(p.id);
            result.push_back({1, key, args});
        } else {
            result.push_back({2, key, {"/usr/bin/pkexec", kLuxuryStoreHelper, action == "Remove" ? "--snap-remove" : action == "Update" ? "--snap-refresh" : "--snap-install", p.id}});
        }
    }
    return result;
}
static void run_operations(std::vector<Operation> ops) {
    if (ops.empty()) return;
    for (auto &operation : ops) {
        if (operation.source != 1 && !g_file_test(kLuxuryStoreHelper, G_FILE_TEST_IS_EXECUTABLE)) {
            message("Install LuxuryStore first", "The authenticated helper is installed by the LuxuryStore Debian package. This source build can browse and review changes."); return;
        }
    }
    auto window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(window, "LuxuryStore — Applying changes");
    gtk_window_set_transient_for(window, app.window); gtk_window_set_modal(window, true);
    gtk_window_set_default_size(window, 850, 500);
    auto terminal = VTE_TERMINAL(vte_terminal_new());
    vte_terminal_set_scrollback_lines(terminal, 10000);
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    for (auto margin : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) margin(box, 10);
    gtk_window_set_child(window, box);
    auto t = new Transaction{window, terminal, std::move(ops)};
    auto heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    t->spinner = gtk_spinner_new(); gtk_spinner_start(GTK_SPINNER(t->spinner));
    gtk_box_append(GTK_BOX(heading), t->spinner);
    t->status = gtk_label_new("Starting package operation…");
    gtk_label_set_wrap(GTK_LABEL(t->status), true); gtk_label_set_xalign(GTK_LABEL(t->status), 0);
    gtk_box_append(GTK_BOX(heading), t->status); gtk_box_append(GTK_BOX(box), heading);
    t->progress = gtk_progress_bar_new(); gtk_box_append(GTK_BOX(box), t->progress);
    auto caption = gtk_label_new("Package operation details");
    gtk_label_set_xalign(GTK_LABEL(caption), 0); gtk_widget_add_css_class(caption, "heading");
    gtk_box_append(GTK_BOX(box), caption);
    auto frame = gtk_frame_new(nullptr); gtk_widget_set_vexpand(frame, true);
    gtk_frame_set_child(GTK_FRAME(frame), scroll(GTK_WIDGET(terminal)));
    gtk_box_append(GTK_BOX(box), frame);
    t->close = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(t->close, GTK_ALIGN_END); gtk_widget_set_sensitive(t->close, false);
    g_signal_connect_swapped(t->close, "clicked", G_CALLBACK(gtk_window_close), window);
    gtk_box_append(GTK_BOX(box), t->close);
    g_object_set_data_full(G_OBJECT(window), "transaction", t, +[](gpointer p) { delete static_cast<Transaction *>(p); });
    g_signal_connect(window, "close-request", G_CALLBACK(+[](GtkWindow *, gpointer data) -> gboolean { return static_cast<Transaction *>(data)->running; }), t);
    g_signal_connect(terminal, "child-exited", G_CALLBACK(+[](VteTerminal *, int status, gpointer data) {
        auto t = static_cast<Transaction *>(data);
        /* Invariant: "child-exited" fires exactly once per spawned operation,
         * in order, so t->next always indexes the operation that just
         * finished. A completion with t->next outside the operations vector
         * means a spurious or duplicate signal; fail loudly instead of
         * misindexing t->operations. */
        g_assert(t->next < t->operations.size());
        if (t->next >= t->operations.size()) { finish_transaction(t, false); return; }
        if (status != 0) { finish_transaction(t, false); return; }
        auto &op = t->operations[t->next];
        if (op.source == 0 && op.key.empty()) app.package_state.requests.clear();
        else if (op.source == 1 || op.source == 2) app.store_state.requests.erase(op.key);
        ++t->next;
        next_operation(t);
    }), t);
    app.busy = true; update_status(); selected_changed();
    gtk_widget_set_sensitive(gtk_window_get_child(app.window), false);
    gtk_window_present(window); next_operation(t);
}
static void show_review() {
    /* Two concurrent review windows would each spawn a pkexec apply on the
     * same package lock; only one may exist at a time. */
    if (app.review_open) return;
    app.review_open = true;
    auto window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(window, "LuxuryStore — Review changes");
    gtk_window_set_transient_for(window, app.window); gtk_window_set_modal(window, true);
    gtk_window_set_default_size(window, 850, 520);
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14); gtk_window_set_child(window, box);
    for (auto set_margin : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) set_margin(box, 10);
    auto review = combined_plan();
    size_t removals = 0;
    std::istringstream lines(review); std::string line;
    while (std::getline(lines, line)) if (line.rfind("Remove\t", 0) == 0) ++removals;
    auto warning = removals ? std::to_string(removals) + " packages will be removed. Review the full list before applying." : "Review the marked changes and APT dependencies.";
    warning += " Stores confirm their final dependencies in the progress window.";
    auto intro = gtk_label_new(warning.c_str());
    if (removals) gtk_widget_add_css_class(intro, "error");
    gtk_label_set_wrap(GTK_LABEL(intro), true); gtk_label_set_xalign(GTK_LABEL(intro), 0);
    gtk_box_append(GTK_BOX(box), intro);
    auto text = gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(text), false);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text), true);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)), review.c_str(), review.size());
    gtk_box_append(GTK_BOX(box), scroll(text));
    auto buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6); gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), buttons);
    auto cancel = gtk_button_new_with_label("Cancel"); gtk_box_append(GTK_BOX(buttons), cancel);
    auto apply = gtk_button_new_with_label("Apply changes"); gtk_box_append(GTK_BOX(buttons), apply); gtk_widget_add_css_class(apply, removals ? "destructive-action" : "suggested-action");
    auto ops = new std::vector<Operation>(operations());
    g_object_set_data_full(G_OBJECT(window), "operations", ops, +[](gpointer p) { delete static_cast<std::vector<Operation> *>(p); });
    g_object_set_data_full(G_OBJECT(window), "review-open", new bool(true), +[](gpointer p) { delete static_cast<bool *>(p); });
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget *widget, gpointer data) {
        (void)data; app.review_open = false;
    }), nullptr);
    g_signal_connect(cancel, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) { gtk_window_destroy(GTK_WINDOW(data)); }), window);
    g_signal_connect(apply, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
        auto window = GTK_WINDOW(data);
        auto ops = *static_cast<std::vector<Operation> *>(g_object_get_data(G_OBJECT(window), "operations"));
        app.review_open = false;
        gtk_window_destroy(window); run_operations(std::move(ops));
    }), window);
    gtk_window_present(window);
}
static void source_changed(GtkCheckButton *button, gpointer data) {
    int source = GPOINTER_TO_INT(data);
    if (source && gtk_check_button_get_active(button)) {
        const char *binary = source == 1 ? "/usr/bin/flatpak" : "/usr/bin/snap";
        if (g_file_test(binary, G_FILE_TEST_IS_EXECUTABLE)) {
            load_store(source);
        }
        else {
            gtk_check_button_set_active(button, false);
            auto d = gtk_alert_dialog_new("%s setup", source == 1 ? "Flathub" : "Snap Store");
            gtk_alert_dialog_set_detail(d, "The required runtime is missing. Mark it for installation, review the APT changes, then enable this source after installation.");
            const char *buttons[] = {"Cancel", "Mark runtime for installation", nullptr};
            gtk_alert_dialog_set_buttons(d, buttons); gtk_alert_dialog_set_cancel_button(d, 0);
            gtk_alert_dialog_choose(d, app.window, nullptr, +[](GObject *o, GAsyncResult *r, gpointer data) {
                GError *error = nullptr;
                int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(o), r, &error);
                if (choice == 1) mark(GPOINTER_TO_INT(data) == 1 ? "flatpak" : "snapd", "install");
                g_clear_error(&error);
            }, data);
            g_object_unref(d);
        }
    } else if (source) {
        ++app.store_state.generations[source];
        if (app.store_state.cancellables[source])
            g_cancellable_cancel(app.store_state.cancellables[source]);
    }
    refilter();
}
struct StoreLoad { int source; unsigned generation; std::string query; bool force; std::vector<StorePackage> packages; };
static void load_store(int source, bool force) {
    if (app.store_state.cancellables[source])
        g_cancellable_cancel(app.store_state.cancellables[source]);
    g_clear_object(&app.store_state.cancellables[source]);
    app.store_state.cancellables[source] = g_cancellable_new();
    auto work = new StoreLoad{source, ++app.store_state.generations[source], app.store_state.query, force, {}};
    ++app.store_state.jobs; update_status();
    auto task = g_task_new(app.window ? G_OBJECT(app.window) : nullptr, app.store_state.cancellables[source], +[](GObject *, GAsyncResult *result, gpointer) {
        auto task = G_TASK(result);
        auto work = static_cast<StoreLoad *>(g_task_get_task_data(task));
        --app.store_state.jobs;
        GError *error = nullptr;
        bool ok = g_task_propagate_boolean(task, &error);
        if (app.closing) { g_clear_error(&error); return; }
        if (work->generation != app.store_state.generations[work->source]) { g_clear_error(&error); update_status(); return; }
        if (!ok) {
            std::string what = error ? error->message : "";
            // Adding a Flatpak remote changes system configuration, so ask
            // the user first instead of running remote-add implicitly.
            if (work->source == 1 && what.find("not configured") != std::string::npos) {
                g_clear_error(&error); update_status();
                auto d = gtk_alert_dialog_new("Add the Flathub remote?");
                gtk_alert_dialog_set_detail(d, "Browsing Flathub adds the official Flathub remote and downloads the full application catalogue. Add it now?");
                const char *buttons[] = {"Cancel", "Add Flathub", nullptr};
                gtk_alert_dialog_set_buttons(d, buttons); gtk_alert_dialog_set_cancel_button(d, 0);
                gtk_alert_dialog_choose(d, app.window, nullptr, +[](GObject *o, GAsyncResult *r, gpointer data) {
                    GError *error = nullptr;
                    int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(o), r, &error);
                    g_clear_error(&error);
                    int source = GPOINTER_TO_INT(data);
                    if (choice == 1) {
                        ++app.store_state.jobs;
                        app.store_state.mutation = true;
                        update_status();
                        auto add = g_task_new(app.window ? G_OBJECT(app.window) : nullptr, app.lifetime_cancel, +[](GObject *, GAsyncResult *add_result, gpointer) {
                            GError *add_error = nullptr;
                            bool added = g_task_propagate_boolean(G_TASK(add_result), &add_error);
                            --app.store_state.jobs;
                            app.store_state.mutation = false;
                            if (app.closing) { g_clear_error(&add_error); return; }
                            if (!added) {
                                message("Could not add Flathub", add_error ? add_error->message : "Unknown error");
                                gtk_check_button_set_active(app.sources[1], FALSE);
                                g_clear_error(&add_error);
                                return;
                            }
                            load_store(1, true);
                        }, nullptr);
                        g_task_run_in_thread(add, +[](GTask *add_task, gpointer, gpointer, GCancellable *) {
                            try { flathub_add(); g_task_return_boolean(add_task, true); }
                            catch (const std::exception &e) { g_task_return_new_error(add_task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", e.what()); }
                        });
                        g_object_unref(add);
                    } else {
                        gtk_check_button_set_active(app.sources[source], FALSE);
                    }
                }, GINT_TO_POINTER(work->source));
                g_object_unref(d); return;
            }
            message("Store search failed", what); g_clear_error(&error); update_status(); return;
        }
        // Remove only this source; APT rows and other store results remain intact.
        for (guint i = g_list_model_get_n_items(G_LIST_MODEL(app.store)); i > 0; --i) {
            auto object = g_list_model_get_item(G_LIST_MODEL(app.store), i - 1);
            bool remove = row(object)->source() == work->source;
            g_object_unref(object);
            if (remove) g_list_store_remove(app.store, i - 1);
        }
        std::vector<gpointer> objects;
        for (auto &p : work->packages) {
            auto r = new Row{nullptr, p.title.empty() ? p.id : p.title, p.summary, fold(p.id + " " + p.title + " " + p.summary), std::make_unique<StorePackage>(p)};
            auto object = g_object_new(package_row_get_type(), nullptr);
            g_object_set_data_full(G_OBJECT(object), "row", r, +[](gpointer data) { delete static_cast<Row *>(data); });
            objects.push_back(object);
        }
        g_list_store_splice(app.store, g_list_model_get_n_items(G_LIST_MODEL(app.store)), 0, objects.data(), objects.size());
        for (auto object : objects) g_object_unref(object);
        // LuxUp hand-off: mark this channel's updates once, as it loads.
        if (app.store_state.auto_mark && !app.store_state.marks_applied[work->source]) {
            app.store_state.marks_applied[work->source] = true;
            bool changed = false;
            for (auto &p : work->packages) {
                if (!p.upgradable) continue;
                auto key = store_key(p);
                if (!app.store_state.requests.count(key)) { app.store_state.requests[key] = std::make_pair(p, "Update"); changed = true; }
            }
            if (changed) refresh_rows();
        }
        refilter();
    }, nullptr);
    g_task_set_task_data(task, work, +[](gpointer p) { delete static_cast<StoreLoad *>(p); });
    g_task_run_in_thread(task, +[](GTask *task, gpointer, gpointer data, GCancellable *cancellable) {
        auto work = static_cast<StoreLoad *>(data);
        try {
            work->packages = work->source == 1
                ? flatpak_packages(work->force, cancellable)
                : snap_packages(work->query, cancellable);
            g_task_return_boolean(task, true);
        } catch (const std::exception &e) { g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", e.what()); }
    });
    g_object_unref(task);
}
static void render_cell(GtkListItem *item) {
    auto r = row(gtk_list_item_get_item(item));
    std::string text;
    auto label = gtk_list_item_get_child(item);
    gtk_widget_remove_css_class(label, "luxury-mark-install");
    gtk_widget_remove_css_class(label, "luxury-mark-remove");
    gtk_widget_set_tooltip_text(label, nullptr);
    if (!r->repository_id.empty()) {
        auto repo = repository_by_id(r->repository_id);
        switch (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "field"))) {
            case 0: text = "Enable source"; break;
            case 1: text = r->name; break;
            case 4: text = repo ? repo->title : "APT"; break;
            case 5: text = r->summary; break;
            default: break;
        }
        label_text(label, text);
        return;
    }
    switch (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "field"))) {
        case 0: text = r->package ? status_for(r) : (!row_action(r).empty() ? row_action(r) : (r->store->installed.empty() ? "Available" : r->store->upgradable ? "Upgradable" : "Installed")); break;
        case 1: text = r->name; break;
        case 2: text = r->package ? r->installed_version : r->store->installed; break;
        case 3: text = r->package ? r->available_version : r->store->available; break;
        case 4: text = r->source() == 0 ? "APT" : r->source() == 1 ? "Flathub" : "Snap Store"; break;
        case 5: text = r->summary; break;
    }
    // List cells are recycled; styling was reset above before binding a row.
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "field")) == 0) {
        auto action = row_action(r);
        if (!action.empty()) {
            bool removing = action == "Remove";
            gtk_widget_add_css_class(label, removing ? "luxury-mark-remove" : "luxury-mark-install");
            text = (removing ? "− " : "+ ") + text;
            gtk_widget_set_tooltip_text(label, removing ? "Marked for removal — pending Apply" : "Marked for installation or update — pending Apply");
        }
    }
    label_text(label, text);
}
static void add_column(GtkColumnView *view, const char *title, int field, int width, bool expand = false) {
    auto factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory *, GtkListItem *item, gpointer) {
        auto label = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_margin_start(label, 6); gtk_widget_set_margin_end(label, 6);
        gtk_widget_set_margin_top(label, 6); gtk_widget_set_margin_bottom(label, 6);
        gtk_list_item_set_child(item, label);
    }), nullptr);
    g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory *, GtkListItem *item, gpointer data) {
        g_object_set_data(G_OBJECT(item), "field", data);
        auto object = gtk_list_item_get_item(item);
        render_cell(item);
        g_signal_connect_object(object, "changed", G_CALLBACK(render_cell), item, G_CONNECT_SWAPPED);
    }), GINT_TO_POINTER(field));
    g_signal_connect(factory, "unbind", G_CALLBACK(+[](GtkSignalListItemFactory *, GtkListItem *item, gpointer) {
        // A recycled cell must stop observing its previous package.
        auto object = gtk_list_item_get_item(item);
        if (object) g_signal_handlers_disconnect_by_data(object, item);
    }), nullptr);
    auto column = gtk_column_view_column_new(title, GTK_LIST_ITEM_FACTORY(factory));
    gtk_column_view_column_set_fixed_width(column, width);
    gtk_column_view_column_set_resizable(column, true); gtk_column_view_column_set_expand(column, expand);
    gtk_column_view_append_column(view, column); g_object_unref(column);
}
struct Loaded { std::unique_ptr<RPackageLister> apt; RPackageLister::pkgState baseline; std::vector<Row *> rows; std::string resolve_error; ~Loaded() { for (auto r : rows) delete r; } };
static void load() {
    app.busy = true;
    gtk_widget_set_sensitive(app.search, false);
    for (auto b : app.sources) gtk_widget_set_sensitive(GTK_WIDGET(b), false);
    label_text(app.status, "Reading APT package lists…");
    gtk_widget_set_sensitive(app.review, false); gtk_widget_set_sensitive(app.clear, false); selected_changed();
    auto task = g_task_new(app.window ? G_OBJECT(app.window) : nullptr, app.lifetime_cancel, +[](GObject *, GAsyncResult *result, gpointer) {
        std::lock_guard<std::recursive_mutex> lock(apt_mutex);
        GError *error = nullptr;
        std::unique_ptr<Loaded> loaded(static_cast<Loaded *>(g_task_propagate_pointer(G_TASK(result), &error)));
        if (app.closing) return;
        app.busy = false;
        if (error) {
            label_text(app.status, error->message);
            message("Cannot read package lists", error->message);
            /* Re-enable the controls the load path disabled up front so a
             * failed cache read never leaves the window permanently dead. */
            gtk_widget_set_sensitive(app.search, true);
            for (auto b : app.sources) gtk_widget_set_sensitive(GTK_WIDGET(b), true);
            g_error_free(error);
            return;
        }
        g_list_store_remove_all(app.store);
        app.package_state.apt = std::move(loaded->apt); app.package_state.baseline = std::move(loaded->baseline);
        if (!loaded->resolve_error.empty()) { app.package_state.requests.clear(); app.package_state.apt->restoreState(app.package_state.baseline); message("Review needs to be rebuilt", loaded->resolve_error); }
        {
            std::vector<gpointer> objects;
            objects.reserve(loaded->rows.size());
            for (auto r : loaded->rows) {
                auto object = g_object_new(package_row_get_type(), nullptr);
                g_object_set_data_full(G_OBJECT(object), "row", r, +[](gpointer p) { delete static_cast<Row *>(p); });
                objects.push_back(object);
            }
            loaded->rows.clear();
            g_list_store_splice(app.store, 0, 0, objects.data(), objects.size());
            for (auto o : objects) g_object_unref(o);
        }
        loaded.reset();
        update_apt_snapshot();
#ifdef __GLIBC__
        // The old catalogue/cache and temporary row-pointer vectors are now
        // released. Return their free pages once at this operation boundary;
        // keep the current model, marks and cache live. No idle allocator timer.
        malloc_trim(0);
#endif
        gtk_widget_set_sensitive(app.search, true);
        for (auto b : app.sources) gtk_widget_set_sensitive(GTK_WIDGET(b), true);
        refilter(); selected_changed();
        if (!app.pending_repository_install.empty()) {
            auto package = app.pending_repository_install;
            app.pending_repository_install.clear();
            if (app.package_state.apt->getPackage(package)) mark(package, "install");
            else message("Package not available", "The repository was enabled, but " + package +
                " is not in the refreshed APT lists. Check the repository's supported release and architecture.");
        }
        if (app.want_mark_updates && !app.busy) { app.want_mark_updates = false; mark_all_updates(); }
        else for (int source = 1; source < 3; ++source)
            if (gtk_check_button_get_active(app.sources[source])) load_store(source);
    }, nullptr);
    g_task_run_in_thread(task, +[](GTask *task, gpointer, gpointer, GCancellable *) {
        try {
            auto loaded = std::make_unique<Loaded>();
            loaded->apt = open_apt();
            for (auto p : loaded->apt->getPackages()) {
                if (p->isMultiArchDuplicate()) continue;
                auto r = new Row{p, safe(p->name()), safe(p->summary()), ""};
                r->search = fold(r->name + " " + r->summary);
                r->package_flags = p->getFlags();
                r->package_action = action_for(p);
                r->installed_version = safe(p->installedVersion());
                r->available_version = safe(p->availableVersion());
                loaded->rows.push_back(r);
            }
            for (const auto &repo : repository_catalog()) {
                if (!repository_file_exists(repo)) continue;
                RepositoryState state;
                try {
                    state = repository_inspect(repository_contents(repo), g_str_has_suffix(repo.file, ".sources"));
                } catch (const std::exception &e) {
                    g_warning("LuxuryStore: ignoring unsafe repository %s: %s", repo.id, e.what());
                    continue;
                }
                if (!state.valid || !state.can_enable || state.enabled) continue;
                auto r = new Row{nullptr, repo.package, repo.description, "", {}, repo.id};
                r->search = fold(r->name + " " + r->summary + " " + repo.title);
                loaded->rows.push_back(r);
            }
            std::sort(loaded->rows.begin(), loaded->rows.end(), [](Row *a, Row *b) { return a->name < b->name; });
            // Resolve retained marks on the worker thread so the solver never
            // blocks the UI; the result is marshalled back above.
            loaded->apt->saveState(loaded->baseline);
            Requests retained;
            {
                std::lock_guard<std::recursive_mutex> lock(apt_mutex);
                retained = app.package_state.requests;
            }
            try { resolve(*loaded->apt, retained); }
            catch (const std::exception &e) {
                loaded->resolve_error = e.what();
                try { loaded->apt->restoreState(loaded->baseline); } catch (...) {}
            }
            g_task_return_pointer(task, loaded.release(), +[](gpointer p) { delete static_cast<Loaded *>(p); });
        } catch (const std::exception &e) { g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", e.what()); }
    });
    g_object_unref(task);
}
static GtkWidget *button(GtkWidget *box, const char *title, GCallback callback) {
    auto b = gtk_button_new_with_label(title);
    g_signal_connect(b, "clicked", callback, nullptr); gtk_box_append(GTK_BOX(box), b); return b;
}
struct RepositoryToggle { std::string id; GtkWindow *window; };
static void show_repositories() {
    if (app.busy) return;
    auto window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(window, "LuxuryStore — Repositories");
    gtk_window_set_transient_for(window, app.window);
    gtk_window_set_modal(window, true);
    gtk_window_set_default_size(window, 540, 460);
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    for (auto margin : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) margin(box, 10);
    gtk_window_set_child(window, box);
    auto intro = gtk_label_new("Choose which configured software repositories are enabled.");
    gtk_label_set_wrap(GTK_LABEL(intro), true);
    gtk_label_set_xalign(GTK_LABEL(intro), 0);
    gtk_box_append(GTK_BOX(box), intro);
    auto list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(box), scroll(list));
    for (const auto &repo : repository_catalog()) {
        auto item = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        auto check = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(repo.title));
        bool present = repository_file_exists(repo);
        bool enabled = false;
        bool can_enable = false;
        bool supported = false;
        std::string reason;
        try {
            if (present) {
                auto state = repository_inspect(repository_contents(repo), g_str_has_suffix(repo.file, ".sources"));
                enabled = state.enabled;
                can_enable = state.can_enable;
                supported = state.valid;
                reason = state.reason;
            }
        } catch (const std::exception &e) {
            g_warning("LuxuryStore: ignoring unsafe repository %s: %s", repo.id, e.what());
            enabled = false;
            can_enable = false;
            reason = "Source definition is unreadable or unsafe";
        }
        gtk_check_button_set_active(check, enabled);
        gtk_widget_set_sensitive(GTK_WIDGET(check), present && supported && (enabled || can_enable));
        gtk_box_append(GTK_BOX(item), GTK_WIDGET(check));
        auto detail = gtk_label_new((std::string(repo.description) + " · " +
            (!present ? "Source definition not installed" :
             !supported || !can_enable ? reason :
             "APT source configured")).c_str());
        gtk_label_set_xalign(GTK_LABEL(detail), 0);
        gtk_label_set_wrap(GTK_LABEL(detail), true);
        gtk_widget_add_css_class(detail, "dim-label");
        gtk_widget_set_margin_start(detail, 26);
        gtk_box_append(GTK_BOX(item), detail);
        gtk_box_append(GTK_BOX(list), item);
        auto toggle = new RepositoryToggle{repo.id, window};
        g_object_set_data_full(G_OBJECT(check), "repo-toggle", toggle,
            +[](gpointer p) { delete static_cast<RepositoryToggle *>(p); });
        g_signal_connect(check, "toggled", G_CALLBACK(+[](GtkCheckButton *check, gpointer data) {
            auto toggle = static_cast<RepositoryToggle *>(data);
            if (app.busy) return;
            auto id = toggle->id;
            bool enable = gtk_check_button_get_active(check);
            gtk_window_destroy(toggle->window);
            run_operations({Operation{3, id, {"/usr/bin/pkexec", kLuxuryStoreHelper,
                "--repo-set", id, enable ? "on" : "off"}}});
        }), toggle);
    }
    auto close = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(close, GTK_ALIGN_END);
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_close), window);
    gtk_box_append(GTK_BOX(box), close);
    gtk_window_present(window);
}
struct RepositoryInstall { std::string id, package; };
static void prompt_repository_install(Row *r) {
    auto repo = repository_by_id(r->repository_id);
    if (!repo) return;
    auto dialog = gtk_alert_dialog_new("Enable %s?", repo->title);
    gtk_alert_dialog_set_detail(dialog,
        ("LuxuryStore will enable this APT source, refresh package lists, then mark " +
         r->name + " for installation. You can review dependencies before applying.").c_str());
    const char *choices[] = {"Cancel", "Enable source", nullptr};
    gtk_alert_dialog_set_buttons(dialog, choices);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    auto request = new RepositoryInstall{repo->id, r->name};
    gtk_alert_dialog_choose(dialog, app.window, nullptr, +[](GObject *object, GAsyncResult *result, gpointer data) {
        std::unique_ptr<RepositoryInstall> request(static_cast<RepositoryInstall *>(data));
        GError *error = nullptr;
        int answer = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(object), result, &error);
        g_clear_error(&error);
        if (answer != 1 || app.busy) return;
        app.pending_repository_install = request->package;
        run_operations({Operation{3, request->id, {"/usr/bin/pkexec", kLuxuryStoreHelper,
            "--repo-set", request->id, "on"}}});
    }, request);
    g_object_unref(dialog);
}
static void open_window(GtkApplication *application, gpointer) {
    if (app.window) { gtk_window_present(app.window); return; }
    app.application = application;
    if (!app.lifetime_cancel) app.lifetime_cancel = g_cancellable_new();
    app.closing = false;
    auto marks_css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(marks_css,
        ".luxury-mark-install { color: #14532d; background-color: #dcfce7; border: 1px solid #22c55e; border-radius: 5px; padding: 3px 7px; font-weight: bold; }"
        ".luxury-mark-remove { color: #7f1d1d; background-color: #fee2e2; border: 1px solid #ef4444; border-radius: 5px; padding: 3px 7px; font-weight: bold; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(marks_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(marks_css);
    app.window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(application)));
    gtk_window_set_title(app.window, "LuxuryStore"); gtk_window_set_default_size(app.window, 1150, 760);
    g_signal_connect(app.window, "close-request", G_CALLBACK(close_requested), nullptr);
    g_signal_connect(app.window, "destroy", G_CALLBACK(+[](GtkWidget *, gpointer) {
        if (app.store_state.search_source) {
            g_source_remove(app.store_state.search_source);
            app.store_state.search_source = 0;
        }
        if (app.lifetime_cancel) g_cancellable_cancel(app.lifetime_cancel);
        for (int source = 1; source < 3; ++source) {
            if (app.store_state.cancellables[source])
                g_cancellable_cancel(app.store_state.cancellables[source]);
            g_clear_object(&app.store_state.cancellables[source]);
        }
        g_clear_object(&app.lifetime_cancel);
        app.window = nullptr;
    }), nullptr);
    auto outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    auto header = gtk_header_bar_new();
    auto title = gtk_label_new("LuxuryStore"); gtk_widget_add_css_class(title, "title"); gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);
    auto repos = gtk_button_new_with_label("Repos");
    gtk_widget_set_tooltip_text(repos, "Manage APT repositories");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), repos);
    g_signal_connect(repos, "clicked", G_CALLBACK(+[](GtkButton *, gpointer) { show_repositories(); }), nullptr);
    auto about = gtk_button_new_with_label("About");
    gtk_widget_set_tooltip_text(about, "About LuxuryStore");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), about);
    g_signal_connect(about, "clicked", G_CALLBACK((+[](GtkButton *, gpointer) {
        static const char *authors[] = {"Alfredo K. Kojima", "Michael Vogt", "Gustavo Niemeyer",
            "Sebastian Heinlein", "Enrico Zini", "Panu Matilainen", "Sviatoslav Sviridov", nullptr};
        static const char *documenters[] = {"Wybo Dekker", "Michael Vogt", "Sebastian Heinlein", nullptr};
        GtkWindow *dialog = luxe_about_new();
        g_object_set(dialog,
                     "program-name", "LuxuryStore",
                     "logo-icon-name", "luxurystore",
                     "version", LUXURYSTORE_VERSION,
                     "comments", "Package management for Luxe, built on Synaptic's APT backend.",
                     "website", "https://github.com/openconstruct/luxeDE",
                     "copyright", "© 2001-2004 Connectiva S/A\n© 2002-2025 Michael Vogt\n© 2026 Jerry Howell",
                     "license-type", GTK_LICENSE_GPL_2_0,
                     NULL);
        gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);
        gtk_about_dialog_set_documenters(GTK_ABOUT_DIALOG(dialog), documenters);
        gtk_about_dialog_set_translator_credits(GTK_ABOUT_DIALOG(dialog), gettext("translator-credits"));
        luxe_about_credits(GTK_ABOUT_DIALOG(dialog));
        luxe_dialog_present(dialog, GTK_WIDGET(app.window));
    })), nullptr);
    // GtkApplicationWindow hosts the header bar in an GtkBox top bar,
    // never via gtk_window_set_titlebar.
    auto toolbar_view = luxe_toolbar_new();
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar_view), header);
    luxe_toolbar_set_content(GTK_BOX(toolbar_view), outer);
    luxe_window_set_content(GTK_WINDOW(app.window), toolbar_view);
    auto toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(toolbar, 10); gtk_widget_set_margin_end(toolbar, 10); gtk_widget_set_margin_top(toolbar, 10); gtk_widget_set_margin_bottom(toolbar, 10);
    gtk_box_append(GTK_BOX(outer), toolbar);
    button(toolbar, "Reload", G_CALLBACK(+[](GtkButton *, gpointer) {
        if (app.busy || app.store_state.jobs) return;
        if (gtk_check_button_get_active(app.sources[0])) run_operations((std::vector<Operation>{Operation{0, "refresh", {"/usr/bin/pkexec", kLuxuryStoreHelper, "--refresh", "apt"}}}));
        for (int source = 1; source < 3; ++source)
            if (gtk_check_button_get_active(app.sources[source])) load_store(source, true);
    }));
    app.install = button(toolbar, "Mark install / upgrade", G_CALLBACK(+[](GtkButton *, gpointer) {
        auto r = row(gtk_single_selection_get_selected_item(app.selection)); if (r) { if (!r->repository_id.empty()) prompt_repository_install(r); else if (r->package) mark(r->name, "install"); else { app.store_state.requests[store_key(*r->store)] = std::make_pair(*r->store, r->store->installed.empty() ? "Install" : "Update"); refresh_rows(); } }
    }));
    app.remove = button(toolbar, "Mark removal", G_CALLBACK(+[](GtkButton *, gpointer) {
        auto r = row(gtk_single_selection_get_selected_item(app.selection)); if (r && r->repository_id.empty()) { if (r->package) mark(r->name, "remove"); else { app.store_state.requests[store_key(*r->store)] = std::make_pair(*r->store, "Remove"); refresh_rows(); } }
    }));
    app.clear = button(toolbar, "Clear marks", G_CALLBACK(+[](GtkButton *, gpointer) {
        if (app.busy || app.package_state.resolving) return;
        {
            std::lock_guard<std::recursive_mutex> lock(apt_mutex);
            app.package_state.requests.clear(); app.store_state.requests.clear(); app.store_state.auto_mark = false;
            for (auto &applied : app.store_state.marks_applied) applied = false;
            if (app.package_state.apt) app.package_state.apt->restoreState(app.package_state.baseline);
        }
        update_apt_snapshot();
        refresh_rows();
    }));
    app.review = button(toolbar, "Review changes", G_CALLBACK(+[](GtkButton *, gpointer) { show_review(); }));
    gtk_widget_add_css_class(app.review, "suggested-action");
    app.search = gtk_search_entry_new(); gtk_widget_set_hexpand(app.search, true);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(app.search), "Search packages"); gtk_box_append(GTK_BOX(toolbar), app.search);
    auto filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(filters, 10); gtk_widget_set_margin_end(filters, 10); gtk_widget_set_margin_bottom(filters, 10);
    gtk_box_append(GTK_BOX(outer), filters);
    gtk_box_append(GTK_BOX(filters), gtk_label_new("Sources"));
    const char *names[] = {"APT", "Flathub", "Snap Store"};
    for (int i = 0; i < 3; ++i) {
        app.sources[i] = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(names[i]));
        // Installed Flatpak is the availability gate, as in LuxUp. The APT
        // load callback loads selected stores after the window is complete.
        gtk_check_button_set_active(app.sources[i], i == 0 ||
            (i == 1 && g_file_test("/usr/bin/flatpak", G_FILE_TEST_IS_EXECUTABLE)));
        gtk_box_append(GTK_BOX(filters), GTK_WIDGET(app.sources[i]));
        g_signal_connect(app.sources[i], "toggled", G_CALLBACK(source_changed), GINT_TO_POINTER(i));
    }
    const char *states[] = {"All packages", "Installed", "Not installed", "Upgradable", "Marked changes", nullptr};
    app.status_filter = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(states));
    gtk_box_append(GTK_BOX(filters), GTK_WIDGET(app.status_filter));
    app.store = g_list_store_new(package_row_get_type());
    app.filter = gtk_custom_filter_new(matches, nullptr, nullptr);
    app.filtered = gtk_filter_list_model_new(G_LIST_MODEL(app.store), GTK_FILTER(app.filter));
    app.selection = gtk_single_selection_new(G_LIST_MODEL(app.filtered));
    gtk_single_selection_set_autoselect(app.selection, false);
    gtk_single_selection_set_can_unselect(app.selection, true);
    auto view = GTK_COLUMN_VIEW(gtk_column_view_new(GTK_SELECTION_MODEL(app.selection)));
    gtk_column_view_set_show_row_separators(view, true);
    add_column(view, "Status", 0, 140); add_column(view, "Package", 1, 230);
    add_column(view, "Installed", 2, 130); add_column(view, "Available", 3, 130);
    add_column(view, "Source", 4, 115); add_column(view, "Description", 5, 260, true);
    auto paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL); gtk_widget_set_vexpand(paned, true);
    gtk_paned_set_start_child(GTK_PANED(paned), scroll(GTK_WIDGET(view))); gtk_paned_set_position(GTK_PANED(paned), 400);
    app.details = gtk_label_new("Select a package to see its description.");
    gtk_label_set_xalign(GTK_LABEL(app.details), 0); gtk_label_set_yalign(GTK_LABEL(app.details), 0);
    gtk_label_set_wrap(GTK_LABEL(app.details), true); gtk_label_set_selectable(GTK_LABEL(app.details), true);
    gtk_widget_set_margin_start(app.details, 10); gtk_widget_set_margin_end(app.details, 10);
    gtk_widget_set_margin_top(app.details, 10); gtk_widget_set_margin_bottom(app.details, 10);
    gtk_paned_set_end_child(GTK_PANED(paned), scroll(app.details)); gtk_box_append(GTK_BOX(outer), paned);
    app.status = gtk_label_new("Reading APT package lists…"); gtk_label_set_xalign(GTK_LABEL(app.status), 0);
    gtk_widget_set_margin_start(app.status, 10); gtk_widget_set_margin_top(app.status, 6); gtk_widget_set_margin_bottom(app.status, 6);
    gtk_box_append(GTK_BOX(outer), app.status);
    g_signal_connect(app.selection, "notify::selected-item", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer) { selected_changed(); }), nullptr);
    g_signal_connect(app.status_filter, "notify::selected", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer) { refilter(); }), nullptr);
    g_signal_connect(app.search, "search-changed", G_CALLBACK(+[](GtkSearchEntry *, gpointer) {
        app.store_state.query = fold(gtk_editable_get_text(GTK_EDITABLE(app.search))); retokenize_query(); refilter();
        ++app.store_state.generations[2];
        if (app.store_state.cancellables[2])
            g_cancellable_cancel(app.store_state.cancellables[2]);
        /* Store searches are network round trips; queue one after typing
         * settles instead of one per keystroke, and cancel the superseded
         * subprocess immediately rather than merely discarding its result. */
        if (app.store_state.search_source) g_source_remove(app.store_state.search_source);
        app.store_state.search_source = g_timeout_add(350, +[](gpointer) -> gboolean {
            app.store_state.search_source = 0;
            if (!app.closing && gtk_check_button_get_active(app.sources[2])) load_store(2);
            return G_SOURCE_REMOVE;
        }, nullptr);
    }), nullptr);
    gtk_window_present(app.window); load();
}
// Authenticate before constructing the package interface. The GTK application
// remains unprivileged; each system operation still uses its pkexec helper.
struct Authentication {
    GtkApplication *application;
    GtkWindow *window;
    GtkWidget *status, *retry, *spinner;
    bool running = false;
};
static Authentication *authentication = nullptr;
static void authentication_finished(GObject *object, GAsyncResult *result, gpointer data) {
    auto a = static_cast<Authentication *>(data);
    GError *error = nullptr;
    bool success = g_subprocess_wait_check_finish(G_SUBPROCESS(object), result, &error);
    a->running = false;
    gtk_spinner_stop(GTK_SPINNER(a->spinner));
    if (success) {
        auto application = a->application;
        // Keep an application window alive across the transition.
        open_window(application, nullptr);
        gtk_window_destroy(a->window);
        authentication = nullptr;
    } else {
        label_text(a->status, "Authentication was cancelled or failed. Authenticate to open LuxuryStore.");
        gtk_widget_set_sensitive(a->retry, true);
    }
    g_clear_error(&error);
}
static void authenticate(Authentication *a) {
    if (a->running) return;
    GError *error = nullptr;
    auto process = g_subprocess_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE),
        &error, "/usr/bin/pkexec", "--disable-internal-agent", kLuxuryStoreHelper, "--authorize", nullptr);
    if (!process) {
        label_text(a->status, "Could not start authentication. Install the LuxuryStore package and try again.");
        g_clear_error(&error); return;
    }
    a->running = true;
    label_text(a->status, "Authenticate to open LuxuryStore.");
    gtk_widget_set_sensitive(a->retry, false); gtk_spinner_start(GTK_SPINNER(a->spinner));
    g_subprocess_wait_check_async(process, nullptr, authentication_finished, a);
    g_object_unref(process);
}
static Authentication *create_authentication(GtkApplication *application) {
    auto window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(application)));
    gtk_window_set_title(window, "LuxuryStore — Authentication required");
    gtk_window_set_default_size(window, 440, 180);
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    for (auto margin : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) margin(box, 10);
    luxe_window_set_content(GTK_WINDOW(window), box);
    auto a = new Authentication{application, window, gtk_label_new("Authenticate to open LuxuryStore."),
        gtk_button_new_with_label("Authenticate"), gtk_spinner_new()};
    authentication = a;
    gtk_label_set_wrap(GTK_LABEL(a->status), true);
    gtk_box_append(GTK_BOX(box), a->spinner); gtk_box_append(GTK_BOX(box), a->status);
    gtk_box_append(GTK_BOX(box), a->retry);
    g_object_set_data_full(G_OBJECT(window), "authentication", a, +[](gpointer p) { delete static_cast<Authentication *>(p); });
    g_signal_connect(a->retry, "clicked", G_CALLBACK(+[](GtkButton *, gpointer p) { authenticate(static_cast<Authentication *>(p)); }), a);
    g_signal_connect(window, "close-request", G_CALLBACK(+[](GtkWindow *, gpointer p) -> gboolean {
        auto a = static_cast<Authentication *>(p);
        if (a->running) return true;
        authentication = nullptr;
        return false;
    }), a);
    gtk_window_present(window);
    return a;
}
static void activate(GtkApplication *application, gpointer) {
    if (app.window) {
        gtk_window_present(app.window);
        // LuxUp may have refreshed APT lists since this window opened.
        // Re-read them, preserving marks, before consuming the hand-off.
        reload_pending_updates();
        return;
    }
    if (authentication) { gtk_window_present(authentication->window); return; }
    authenticate(create_authentication(application));
}
// LuxUp launches LuxuryStore with --mark-updates.  HANDLES_COMMAND_LINE lets
// a second invocation reach the running instance with its argument intact.
static int command_line(GApplication *application, GApplicationCommandLine *command, gpointer) {
    int argc = 0;
    char **argv = g_application_command_line_get_arguments(command, &argc);
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--mark-updates") app.want_mark_updates = true;
    g_strfreev(argv);
    g_application_activate(application);
    return 0;
}
int main(int argc, char **argv) {
    luxe_ui_init();
    auto application = luxe_application_new("org.luxe.LuxuryStore", G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(application, "command-line", G_CALLBACK(command_line), nullptr);
    g_signal_connect(application, "activate", G_CALLBACK(activate), nullptr);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application); return status;
}
