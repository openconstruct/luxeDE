/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define main application_main
#include "../src/main.c"
#undef main
#include "../src/xfce-taskmanager-linux.c"
static GArray *fixture_tasks;
static gboolean fixture_task_failure, fixture_memory_failure;
static gboolean fixture_cpu_failure;
static guint fixture_collection_delay_ms;
static gint fixture_collection_active;
static gint fixture_collection_peak;
static GThread *fixture_main_thread;
static gboolean fixture_memory_values, fixture_memory_available;
static GArray *read_test_tasks(void)
{
    if (fixture_task_failure) return NULL;
    if (!fixture_tasks) return get_task_list();
    GArray *copy = g_array_new(FALSE, FALSE, sizeof(struct task));
    g_array_append_vals(copy, fixture_tasks->data, fixture_tasks->len);
    return copy;
}
static GArray *read_test_tasks_with_options(gboolean full_path)
{
    g_assert_true(g_thread_self() != fixture_main_thread);
    gint active = g_atomic_int_add(&fixture_collection_active, 1) + 1;
    gint peak;
    do {
        peak = g_atomic_int_get(&fixture_collection_peak);
        if (active <= peak) break;
    } while (!g_atomic_int_compare_and_exchange(&fixture_collection_peak, peak, active));
    if (fixture_collection_delay_ms)
        g_usleep((gulong)fixture_collection_delay_ms * 1000);
    (void)full_path;
    GArray *tasks = read_test_tasks();
    g_atomic_int_add(&fixture_collection_active, -1);
    return tasks;
}
static gboolean read_test_system_status(system_status *status)
{
    if (fixture_memory_values) {
        status->mem_total = 1000; status->mem_free = 100;
        status->mem_cached = 100; status->mem_buffered = 100;
        status->mem_available = 700; status->mem_available_valid = fixture_memory_available;
        status->cpu_count = 1;
        return TRUE;
    }
    return fixture_memory_failure ? FALSE : get_system_status(status);
}
static gboolean read_test_cpu(system_status *status)
{
    return fixture_cpu_failure ? FALSE : get_cpu_usage_from_proc(status);
}
#define get_task_list read_test_tasks
#define get_task_list_with_options read_test_tasks_with_options
#define get_system_status read_test_system_status
#define get_cpu_usage_from_proc read_test_cpu
#include "../src/functions.c"
#undef get_task_list
#undef get_task_list_with_options
#undef get_system_status
#undef get_cpu_usage_from_proc
#include "../src/interface.c"
#include "../src/callbacks.c"
#include "../src/utils.c"

static void drain(void)
{
    for (guint i = 0; i < 40; i++) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
}

static void wait_for_refresh(void)
{
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (refresh_in_flight && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    g_assert_false(refresh_in_flight);
}

static gboolean mark_main_loop_responsive(gpointer data)
{
    *(gboolean *)data = TRUE;
    return G_SOURCE_REMOVE;
}

static void refresh_and_wait(void)
{
    g_assert_true(refresh_task_list());
    wait_for_refresh();
}

static void test_parser(void)
{
    const char *memory = "MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 50 kB\nCached: 75 kB\nSReclaimable: 25 kB\nMemAvailable: 700 kB\n";
    FILE *memfile = fmemopen((void *)memory, strlen(memory), "r");
    g_assert_nonnull(memfile);
    system_status status = {0};
    g_assert_true(parse_meminfo(memfile, &status));
    fclose(memfile);
    g_assert_cmpuint(status.mem_total, ==, 1000);
    g_assert_cmpuint(status.mem_free, ==, 100);
    g_assert_cmpuint(status.mem_cached, ==, 100);
    g_assert_true(status.mem_available_valid);
    g_assert_true(status.mem_valid);
    g_assert_cmpuint(status.mem_available, ==, 700);
    const char *incomplete = "MemFree: 100 kB\n";
    memfile = fmemopen((void *)incomplete, strlen(incomplete), "r");
    g_assert_nonnull(memfile);
    g_assert_false(parse_meminfo(memfile, &status));
    g_assert_false(status.mem_valid);
    fclose(memfile);
    const char *invalid[] = {"MemTotal: -1 kB\nMemFree: 1 kB\n",
                             "MemTotal: 18446744073709551616 kB\nMemFree: 1 kB\n",
                             "MemTotal: 1000 kB\nMemFree: bad kB\n",
                             "MemTotal: 1000 kB\nMemTotal: 2000 kB\nMemFree: 1 kB\n"};
    for (guint i = 0; i < G_N_ELEMENTS(invalid); i++) {
        memfile = fmemopen((void *)invalid[i], strlen(invalid[i]), "r");
        g_assert_nonnull(memfile);
        g_assert_false(parse_meminfo(memfile, &status));
        fclose(memfile);
    }
    g_autofree char *long_line = g_strnfill(150, 'x');
    memfile = fmemopen(long_line, strlen(long_line), "r");
    g_assert_nonnull(memfile);
    g_assert_false(parse_meminfo(memfile, &status));
    fclose(memfile);
    struct task task = {0};
    page_size = 4;
    g_assert_true(parse_task_stat("123 (a ) long name) S 12 0 0 0 0 0 0 0 0 0 4294967296 5 0 0 20 -10 1 0 9876543210 1048576 8 0", &task));
    g_assert_cmpstr(task.name, ==, "a ) long name");
    g_assert_cmpuint(task.time, ==, G_GUINT64_CONSTANT(4294967301));
    g_assert_cmpuint(task.start_time, ==, G_GUINT64_CONSTANT(9876543210));
    g_assert_cmpuint(task.rss, ==, 32);
    g_assert_cmpuint(task.size, ==, 1024);
    g_assert_cmpint(task.prio, ==, -10);
    g_assert_false(parse_task_stat("123 (bad) S 1", &task));
    g_assert_false(parse_task_stat("123 missing", &task));
    get_task_details(getpid(), &task);
    g_assert_cmpint(task.pid, ==, getpid());
    g_assert_true(g_utf8_validate(task.name, -1, NULL));
    get_task_details(-123, &task);
    g_assert_cmpint(task.pid, ==, -1);
    char buffer[32];
    g_assert_cmpstr(size_to_string(buffer, 1024), ==, "1.0 KB");
    g_assert_cmpstr(size_to_string(buffer, 1048576), ==, "1.0 MB");
}

static void test_config(void)
{
    g_assert_cmpint(g_mkdir_with_parents(g_get_user_config_dir(), 0700), ==, 0);
    config_file = g_build_filename(g_get_user_config_dir(), "luxetask.conf", NULL);
    g_assert_true(g_file_set_contents(config_file, "# retained\n[General]\nrefresh_interval=0\nwin_width=abc\n[Other]\ncustom=keep\n", -1, NULL));
    load_config();
    g_assert_cmpint(refresh_interval, ==, 1);
    g_assert_cmpint(win_width, ==, 500);
    refresh_interval = 7;
    save_config();
    GKeyFile *file = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(file, config_file, 0, NULL));
    gchar *custom = g_key_file_get_string(file, "Other", "custom", NULL);
    g_assert_cmpstr(custom, ==, "keep");
    g_free(custom);
    g_assert_cmpint(g_key_file_get_integer(file, "General", "refresh_interval", NULL), ==, 7);
    g_key_file_unref(file);
    g_assert_true(g_file_set_contents(config_file, "[malformed", -1, NULL));
    load_config();
    g_assert_cmpint(refresh_interval, ==, 2);
    refresh_interval = 4;
    save_config();
    gchar *text = NULL;
    g_assert_true(g_file_get_contents(config_file, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "[malformed");
    g_free(text);
    g_assert_cmpint(unlink(config_file), ==, 0);
    gchar *target = g_strconcat(config_file, ".target", NULL);
    g_assert_true(g_file_set_contents(target, "[General]\nrefresh_interval=5\n", -1, NULL));
    g_assert_cmpint(symlink(target, config_file), ==, 0);
    load_config();
    refresh_interval = 8;
    save_config();
    load_config();
    g_assert_cmpint(refresh_interval, ==, 5);
    g_assert_cmpint(unlink(config_file), ==, 0);
    g_assert_cmpint(unlink(target), ==, 0);
    g_free(target);
    load_config();
}

static void verify_answer(gint answer, gpointer data)
{
    g_assert_cmpint(answer, ==, 0);
    (*(guint *)data)++;
}

static void test_ui(void)
{
    own_uid = getuid();
    luxetask_app = luxe_application_new("org.luxe.Task.Test", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(luxetask_app), NULL, NULL));
    on_activate(GTK_APPLICATION(luxetask_app), NULL);
    drain();
    GArray *original = task_array;
    guint timer = rID;
    on_activate(GTK_APPLICATION(luxetask_app), NULL);
    g_assert_true(original == task_array);
    g_assert_cmpuint(timer, ==, rID);
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(GTK_APPLICATION(luxetask_app))), ==, 1);
    g_assert_cmpint(gtk_tree_model_get_column_type(GTK_TREE_MODEL(list_store), COLUMN_MEM), ==, G_TYPE_UINT64);
    g_assert_cmpint(gtk_tree_model_get_column_type(GTK_TREE_MODEL(list_store), COLUMN_TIME), ==, G_TYPE_DOUBLE);
    g_assert_cmpint(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(list_store), NULL), >, 0);
    gint previous = refresh_interval;
    show_preferences(GTK_WINDOW(main_window));
    drain();
    GtkWindow *dialog = luxe_window_get_visible_dialog(GTK_WINDOW(main_window));
    g_assert_nonnull(dialog);
    GtkSpinButton *spin = g_object_get_data(G_OBJECT(dialog), "refresh-spin");
    gtk_spin_button_set_value(spin, 9);
    luxe_dialog_close(dialog);
    drain();
    g_assert_cmpint(refresh_interval, ==, previous);
    show_preferences(GTK_WINDOW(main_window));
    drain();
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(main_window));
    spin = g_object_get_data(G_OBJECT(dialog), "refresh-spin");
    gtk_spin_button_set_value(spin, 3);
    on_prefs_apply(NULL, dialog);
    drain();
    g_assert_cmpint(refresh_interval, ==, 3);
    g_assert_cmpuint(rID, !=, timer);
    guint answers = 0;
    luxe_confirm(GTK_WINDOW(main_window), "Test confirmation", NULL, TRUE, verify_answer, &answers);
    drain();
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(main_window));
    g_assert_cmpint(luxe_choice_get_response_appearance(GTK_DIALOG(dialog), "accept"), ==, LUXE_RESPONSE_DESTRUCTIVE);
    luxe_dialog_close(dialog);
    drain();
    g_assert_cmpuint(answers, ==, 1);
    fixture_tasks = g_array_new(FALSE, FALSE, sizeof(struct task));
    struct task sample = { .pid = 12345, .uid = own_uid, .start_time = 1, .size = 1024, .time = 10 };
    g_strlcpy(sample.name, "first", sizeof sample.name);
    g_array_append_val(fixture_tasks, sample);
    fixture_main_thread = g_thread_self();
    fixture_collection_delay_ms = 250;
    gboolean responsive = FALSE;
    g_assert_true(refresh_task_list());
    g_assert_true(refresh_task_list()); /* Coalesce; never start a second scan. */
    g_assert_true(refresh_task_list());
    g_idle_add(mark_main_loop_responsive, &responsive);
    drain();
    g_assert_true(responsive);
    wait_for_refresh();
    fixture_collection_delay_ms = 0;
    g_assert_cmpint(g_atomic_int_get(&fixture_collection_peak), ==, 1);
    g_assert_cmpuint(task_array->len, ==, 1);
    fixture_task_failure = TRUE;
    fixture_memory_failure = TRUE;
    refresh_and_wait();
    g_assert_cmpuint(task_array->len, ==, 1);
    g_assert_nonnull(strstr(gtk_progress_bar_get_text(GTK_PROGRESS_BAR(cpu_usage_progress_bar)), "unavailable"));
    g_assert_cmpstr(gtk_progress_bar_get_text(GTK_PROGRESS_BAR(mem_usage_progress_bar)), ==, "Memory: unknown");
    fixture_task_failure = FALSE;
    refresh_and_wait();
    g_assert_cmpstr(gtk_progress_bar_get_text(GTK_PROGRESS_BAR(mem_usage_progress_bar)), ==, "Memory: unknown");
    fixture_memory_failure = FALSE;
    fixture_cpu_failure = TRUE;
    refresh_and_wait();
    g_assert_cmpstr(gtk_progress_bar_get_text(GTK_PROGRESS_BAR(cpu_usage_progress_bar)), ==, "CPU usage: unknown");
    fixture_cpu_failure = FALSE;
    fixture_memory_values = TRUE;
    fixture_memory_available = TRUE;
    show_cached_as_free = TRUE;
    refresh_and_wait();
    g_assert_cmpfloat_with_epsilon(gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(mem_usage_progress_bar)), 0.3, 0.0001);
    fixture_memory_available = FALSE;
    refresh_and_wait();
    g_assert_cmpfloat_with_epsilon(gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(mem_usage_progress_bar)), 0.7, 0.0001);
    fixture_memory_values = FALSE;
    sample.start_time = 2; /* Simulate PID reuse. */
    g_strlcpy(sample.name, "replacement", sizeof sample.name);
    g_array_index(fixture_tasks, struct task, 0) = sample;
    refresh_and_wait();
    g_assert_cmpuint(task_array->len, ==, 1);
    g_assert_cmpuint(g_array_index(task_array, struct task, 0).start_time, ==, 2);
    GtkTreeIter iter;
    g_assert_true(find_task_row(12345, &iter));
    gchar *name = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(list_store), &iter, COLUMN_NAME, &name, -1);
    g_assert_cmpstr(name, ==, "replacement");
    g_free(name);
    g_strlcpy(g_array_index(fixture_tasks, struct task, 0).name, "after-exec", sizeof sample.name);
    refresh_and_wait();
    g_assert_true(find_task_row(12345, &iter));
    gtk_tree_model_get(GTK_TREE_MODEL(list_store), &iter, COLUMN_NAME, &name, -1);
    g_assert_cmpstr(name, ==, "after-exec");
    g_free(name);
    g_clear_pointer(&fixture_tasks, g_array_unref);
    show_full_path = TRUE;
    fixture_collection_delay_ms = 150;
    change_full_path();
    on_quit();
    gtk_window_destroy(GTK_WINDOW(main_window));
    wait_for_refresh();
    drain();
    g_assert_null(main_window);
    g_assert_cmpuint(rID, ==, 0);
    cleanup_task_view();
    g_clear_pointer(&task_array, g_array_unref);
    g_clear_object(&luxetask_app);
    g_clear_pointer(&sys_stat, g_free);
    g_clear_pointer(&uid_name_cache, g_hash_table_unref);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    luxe_ui_init();
    g_test_add_func("/task/parser", test_parser);
    g_test_add_func("/task/config", test_config);
    g_test_add_func("/task/ui", test_ui);
    return g_test_run();
}
