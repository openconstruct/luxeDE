/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define HAVE_STRING_H 1
#define CLIPBOARD_RECEIVE_TIMEOUT_MS 500U
#define CLIPBOARD_SAVE_TIMEOUT_MS 1100U
#define CLIPBOARD_SEND_TIMEOUT_MS 500U
#include "../luxeclipboard/clipboard.c"

static TargetData *new_target(void)
{
    TargetData *target = g_slice_new0(TargetData);
    target->refcount = 1;
    return target;
}

static void budget_and_lifetime(void)
{
    TargetData *first = new_target(), *second = new_target(), *third = new_target();
    g_assert_true(target_reserve(first, CLIPBOARD_MAX_RETAINED_BYTES / 2));
    g_assert_true(target_reserve(second, CLIPBOARD_MAX_RETAINED_BYTES / 2));
    g_assert_cmpuint(retained_target_bytes, ==, CLIPBOARD_MAX_RETAINED_BYTES);
    g_assert_false(target_reserve(third, 1));
    target_data_ref(first);
    target_data_unref(first); /* An outgoing conversion still retains it. */
    g_assert_false(target_reserve(third, 1));
    target_data_unref(first);
    g_assert_true(target_reserve(third, 1));
    for (gsize n = 2; n <= 10000; n++) g_assert_true(target_reserve(third, n));
    g_assert_cmpuint(third->capacity, >=, 10000);
    g_assert_cmpuint(third->capacity, <, 20000);
    g_assert_false(target_reserve(third, CLIPBOARD_MAX_PROPERTY_BYTES + 2));
    target_data_unref(second);
    target_data_unref(third);
    g_assert_cmpuint(retained_target_bytes, ==, 0);
}

static void duplicate_and_failed_incr(void)
{
    GsdClipboardManager *manager = g_object_new(GSD_TYPE_CLIPBOARD_MANAGER, NULL);
    Display *display = XOpenDisplay(NULL);
    g_assert_nonnull(display);
    manager->priv->display = display;
    init_atoms(display);
    Window root = DefaultRootWindow(display);
    manager->priv->window = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    Window requestor = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    manager->priv->requestor = requestor;
    manager->priv->property = XInternAtom(display, "_LUXE_TEST_SAVE", False);
    manager->priv->time = CurrentTime;
    g_assert_true(create_save_window(manager));
    save_targets(manager, NULL, 0);
    g_assert_cmpuint(manager->priv->requestor, ==, None);
    g_assert_cmpuint(manager->priv->save_window, ==, None);
    manager->priv->requestor = requestor;
    g_assert_true(create_save_window(manager));
    Window first_save_window = manager->priv->save_window;
    Atom a = XInternAtom(display, "_LUXE_TEST_A", False);
    Atom b = XInternAtom(display, "_LUXE_TEST_B", False);
    Atom property = XInternAtom(display, "_LUXE_TEST_TARGETS", False);
    Atom input[] = { b, b, a, a };
    XChangeProperty(display, manager->priv->save_window, property, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)input, 4);
    Atom type; int format; unsigned long count, remaining; unsigned char *targets = NULL;
    g_assert_cmpint(XGetWindowProperty(display, manager->priv->save_window, property, 0, 16,
        True, XA_ATOM, &type, &format, &count, &remaining, &targets), ==, Success);
    g_assert_cmpuint(count, ==, 4);
    save_targets(manager, (Atom *)targets, count);
    g_assert_cmpuint(g_slist_length(manager->priv->contents), ==, 2);
    GSList *item = g_slist_find_custom(manager->priv->contents, &a, (GCompareFunc)find_content_target);
    g_assert_nonnull(item);
    TargetData *oversized = item->data;
    oversized->type = XA_INCR;
    g_assert_true(target_reserve(oversized, CLIPBOARD_MAX_PROPERTY_BYTES + 1));
    oversized->length = CLIPBOARD_MAX_PROPERTY_BYTES;
    unsigned char chunk = 'x';
    XChangeProperty(display, manager->priv->save_window, a, XA_STRING, 8, PropModeReplace, &chunk, 1);
    XEvent event = {0};
    event.xproperty.type = PropertyNotify;
    event.xproperty.display = display;
    event.xproperty.window = manager->priv->save_window;
    event.xproperty.atom = a;
    g_assert_true(receive_incrementally(manager, &event));
    g_assert_null(g_slist_find_custom(manager->priv->contents, &a, (GCompareFunc)find_content_target));
    g_assert_cmpuint(retained_target_bytes, ==, 0);
    g_assert_cmpuint(manager->priv->pending_target, ==, b);
    XEvent late = {0};
    late.xselection.type = SelectionNotify;
    late.xselection.display = display;
    late.xselection.requestor = manager->priv->save_window;
    late.xselection.selection = XA_CLIPBOARD;
    late.xselection.target = a;
    late.xselection.property = None;
    clipboard_manager_process_event(manager, &late);
    g_assert_cmpuint(manager->priv->pending_target, ==, b);
    item = g_slist_find_custom(manager->priv->contents, &b, (GCompareFunc)find_content_target);
    g_assert_nonnull(item);
    ((TargetData *)item->data)->type = XA_STRING;
    request_next_target(manager);
    g_assert_cmpuint(manager->priv->requestor, ==, None);
    g_assert_cmpuint(XGetSelectionOwner(display, XA_CLIPBOARD), ==, manager->priv->window);
    g_slist_free_full(manager->priv->contents, (GDestroyNotify)target_data_unref);
    manager->priv->contents = NULL;
    manager->priv->requestor = requestor;
    g_assert_true(create_save_window(manager));
    event.xproperty.window = manager->priv->save_window;
    TargetData *stalled = new_target();
    stalled->target = a; stalled->type = XA_INCR;
    g_assert_true(target_reserve(stalled, 100));
    manager->priv->contents = g_slist_append(NULL, stalled);
    set_receive_timeout(manager, a);
    late.xselection.requestor = first_save_window;
    late.xselection.target = a;
    clipboard_manager_process_event(manager, &late);
    g_assert_cmpuint(manager->priv->pending_target, ==, a);
    XChangeProperty(display, manager->priv->save_window, a, XA_STRING, 8, PropModeReplace, &chunk, 1);
    g_assert_true(receive_incrementally(manager, &event));
    g_usleep(300000);
    while (g_main_context_iteration(NULL, FALSE));
    XChangeProperty(display, manager->priv->save_window, a, XA_STRING, 8, PropModeReplace, &chunk, 1);
    g_assert_true(receive_incrementally(manager, &event));
    g_usleep(300000);
    while (g_main_context_iteration(NULL, FALSE));
    g_assert_cmpuint(manager->priv->requestor, !=, None);
    gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (manager->priv->requestor != None && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    g_assert_cmpuint(manager->priv->requestor, ==, None);
    g_assert_null(manager->priv->contents);
    g_assert_cmpuint(retained_target_bytes, ==, 0);
    manager->priv->requestor = requestor;
    g_assert_true(create_save_window(manager));
    TargetData *next = new_target(); next->target = b; next->type = XA_STRING;
    manager->priv->contents = g_slist_append(NULL, next);
    request_next_target(manager);
    g_assert_cmpuint(manager->priv->requestor, ==, None);
    g_assert_cmpuint(XGetSelectionOwner(display, XA_CLIPBOARD), ==, manager->priv->window);
    g_slist_free_full(manager->priv->contents, (GDestroyNotify)target_data_unref);
    manager->priv->contents = NULL;
    manager->priv->requestor = requestor;
    g_assert_true(create_save_window(manager));
    event.xproperty.window = manager->priv->save_window;
    TargetData *slow = new_target(); slow->target = a; slow->type = XA_INCR;
    manager->priv->contents = g_slist_append(NULL, slow);
    start_save_timeout(manager);
    set_receive_timeout(manager, a);
    for (int i = 0; i < 3; i++) {
        g_usleep(300000);
        while (g_main_context_iteration(NULL, FALSE));
        g_assert_cmpuint(manager->priv->requestor, !=, None);
        XChangeProperty(display, manager->priv->save_window, a, XA_STRING, 8, PropModeReplace, &chunk, 1);
        g_assert_true(receive_incrementally(manager, &event));
    }
    deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (manager->priv->requestor != None && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    g_assert_cmpuint(manager->priv->requestor, ==, None);
    g_assert_null(manager->priv->contents);
    g_assert_cmpuint(retained_target_bytes, ==, 0);
    TargetData *payload = new_target();
    payload->target = a; payload->type = XA_STRING; payload->format = 8;
    payload->length = 4;
    g_assert_true(target_reserve(payload, 5));
    memcpy(payload->data, "test", 5);
    manager->priv->contents = g_slist_append(NULL, payload);
    gulong old_selection_max = SELECTION_MAX_SIZE;
    SELECTION_MAX_SIZE = 2;
    Atom properties[CLIPBOARD_MAX_OUTGOING_PER_WINDOW];
    for (guint i = 0; i < G_N_ELEMENTS(properties); i++) {
        g_autofree char *name = g_strdup_printf("_LUXE_OUT_%u", i);
        properties[i] = XInternAtom(display, name, False);
        IncrConversion *out = g_slice_new0(IncrConversion);
        out->target = a; out->requestor = requestor; out->property = properties[i]; out->offset = -1;
        convert_clipboard_target(out, manager);
        g_assert_cmpint(out->offset, ==, 0);
        collect_incremental(out, manager);
    }
    g_assert_cmpuint(g_slist_length(manager->priv->conversions), ==, CLIPBOARD_MAX_OUTGOING_PER_WINDOW);
    IncrConversion *extra = g_slice_new0(IncrConversion);
    extra->target = a; extra->requestor = requestor;
    extra->property = XInternAtom(display, "_LUXE_OUT_EXTRA", False); extra->offset = -1;
    convert_clipboard_target(extra, manager);
    g_assert_cmpuint(extra->property, ==, None);
    collect_incremental(extra, manager);
    XEvent ack = {0};
    ack.xproperty.type = PropertyNotify; ack.xproperty.display = display;
    ack.xproperty.window = requestor; ack.xproperty.atom = properties[0];
    for (int i = 0; i < 3; i++) {
        XDeleteProperty(display, requestor, properties[0]);
        g_assert_true(send_incrementally(manager, &ack));
    }
    g_assert_cmpuint(g_slist_length(manager->priv->conversions), ==, CLIPBOARD_MAX_OUTGOING_PER_WINDOW - 1);
    g_assert_false(outgoing_allowed(manager, requestor, properties[1]));
    IncrConversion *synthetic[CLIPBOARD_MAX_OUTGOING];
    guint added = CLIPBOARD_MAX_OUTGOING - g_slist_length(manager->priv->conversions);
    for (guint i = 0; i < added; i++) {
        synthetic[i] = g_slice_new0(IncrConversion);
        synthetic[i]->requestor = 10000 + i;
        synthetic[i]->property = properties[0];
        manager->priv->conversions = g_slist_prepend(manager->priv->conversions, synthetic[i]);
    }
    g_assert_false(outgoing_allowed(manager, 999999, properties[0]));
    for (guint i = 0; i < added; i++) {
        manager->priv->conversions = g_slist_remove(manager->priv->conversions, synthetic[i]);
        conversion_free(synthetic[i]);
    }
    g_slist_free_full(manager->priv->contents, (GDestroyNotify)target_data_unref);
    manager->priv->contents = NULL;
    g_assert_cmpuint(retained_target_bytes, >, 0);
    deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (manager->priv->conversions && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    g_assert_null(manager->priv->conversions);
    g_assert_cmpuint(retained_target_bytes, ==, 0);
    XWindowAttributes attrs;
    g_assert_true(XGetWindowAttributes(display, requestor, &attrs));
    g_assert_false((attrs.your_event_mask & PropertyChangeMask) != 0);
    g_assert_true(outgoing_allowed(manager, requestor, properties[0]));
    manager->priv->requestor = requestor;
    g_assert_true(create_save_window(manager));
    event.xproperty.window = manager->priv->save_window;
    TargetData *mixed = new_target(); mixed->target = a; mixed->type = XA_INCR;
    manager->priv->contents = g_slist_append(NULL, mixed);
    set_receive_timeout(manager, a);
    XChangeProperty(display, manager->priv->save_window, a, XA_STRING, 8, PropModeReplace, &chunk, 1);
    g_assert_true(receive_incrementally(manager, &event));
    unsigned long word = 1;
    XChangeProperty(display, manager->priv->save_window, a, XA_ATOM, 32, PropModeReplace, (unsigned char *)&word, 1);
    g_assert_true(receive_incrementally(manager, &event));
    g_assert_null(manager->priv->contents);
    g_assert_cmpuint(manager->priv->requestor, ==, None);
    manager->priv->requestor = requestor;
    g_assert_true(create_save_window(manager));
    event.xproperty.window = manager->priv->save_window;
    TargetData *empty = new_target(); empty->target = a; empty->type = XA_INCR;
    manager->priv->contents = g_slist_append(NULL, empty);
    set_receive_timeout(manager, a);
    XDeleteProperty(display, manager->priv->save_window, a);
    g_assert_true(receive_incrementally(manager, &event));
    g_assert_null(manager->priv->contents);
    g_assert_cmpuint(manager->priv->requestor, ==, None);
    TargetData *victim_payload = new_target();
    victim_payload->target = a; victim_payload->type = XA_STRING; victim_payload->format = 8;
    victim_payload->length = 4;
    g_assert_true(target_reserve(victim_payload, 5));
    memcpy(victim_payload->data, "test", 5);
    manager->priv->contents = g_slist_append(NULL, victim_payload);
    Window victim = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
    SELECTION_MAX_SIZE = 2;
    IncrConversion *victim_conversion = g_slice_new0(IncrConversion);
    victim_conversion->target = a; victim_conversion->requestor = victim;
    victim_conversion->property = properties[0]; victim_conversion->offset = -1;
    convert_clipboard_target(victim_conversion, manager);
    g_assert_cmpint(victim_conversion->offset, ==, 0);
    XDestroyWindow(display, victim); XSync(display, False);
    XEvent destroyed = {0};
    destroyed.xdestroywindow.type = DestroyNotify; destroyed.xdestroywindow.window = victim;
    clipboard_manager_process_event(manager, &destroyed);
    g_assert_null(manager->priv->conversions);
    g_slist_free_full(manager->priv->contents, (GDestroyNotify)target_data_unref);
    manager->priv->contents = NULL;
    g_assert_cmpuint(retained_target_bytes, ==, 0);
    g_assert_true(create_save_window(manager));
    TargetData *bad_header = new_target(); bad_header->target = a;
    manager->priv->contents = g_slist_append(NULL, bad_header);
    XChangeProperty(display, manager->priv->save_window, a, XA_INCR, 8, PropModeReplace, &chunk, 1);
    get_property(bad_header, manager);
    g_assert_null(manager->priv->contents);
    bad_header = new_target(); bad_header->target = a;
    manager->priv->contents = g_slist_append(NULL, bad_header);
    unsigned long announced = CLIPBOARD_MAX_PROPERTY_BYTES + 1;
    XChangeProperty(display, manager->priv->save_window, a, XA_INCR, 32, PropModeReplace,
                    (unsigned char *)&announced, 1);
    get_property(bad_header, manager);
    g_assert_null(manager->priv->contents);
    destroy_save_window(manager);
    TargetData *words = new_target(); words->target = a; words->type = XA_INTEGER; words->format = 32;
    unsigned long values[] = { 0x12345678UL, 0x76543210UL };
    words->length = sizeof values;
    g_assert_true(target_reserve(words, sizeof values + 1));
    memcpy(words->data, values, sizeof values);
    manager->priv->contents = g_slist_append(NULL, words);
    SELECTION_MAX_SIZE = sizeof(unsigned long) + 2;
    IncrConversion *word_conversion = g_slice_new0(IncrConversion);
    word_conversion->target = a; word_conversion->requestor = requestor;
    word_conversion->property = properties[0]; word_conversion->offset = -1;
    convert_clipboard_target(word_conversion, manager);
    g_assert_cmpint(word_conversion->offset, ==, 0);
    XDeleteProperty(display, requestor, properties[0]);
    for (guint i = 0; i < G_N_ELEMENTS(values); i++) {
        g_assert_true(send_incrementally(manager, &ack));
        unsigned char *result = NULL;
        g_assert_cmpint(XGetWindowProperty(display, requestor, properties[0], 0, 10, True,
                                           XA_INTEGER, &type, &format, &count, &remaining, &result), ==, Success);
        g_assert_cmpint(format, ==, 32);
        g_assert_cmpuint(count, ==, 1);
        g_assert_cmpuint(((unsigned long *)result)[0], ==, values[i]);
        XFree(result);
    }
    g_assert_true(send_incrementally(manager, &ack));
    g_assert_null(manager->priv->conversions);
    g_slist_free_full(manager->priv->contents, (GDestroyNotify)target_data_unref);
    manager->priv->contents = NULL;
    g_assert_cmpuint(retained_target_bytes, ==, 0);
    SELECTION_MAX_SIZE = old_selection_max;
    XDestroyWindow(display, requestor);
    g_object_unref(manager);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/clipboard/budget-and-lifetime", budget_and_lifetime);
    g_test_add_func("/clipboard/duplicate-and-failed-incr", duplicate_and_failed_incr);
    return g_test_run();
}
