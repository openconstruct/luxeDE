/*
 * Copyright (C) 2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 * Copyright (C) 2026 Jerry Howell
 *
 * This file is a part of LXHotkey project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "luxehotkey.h"
#include "shortcut-validate.h"

#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <stdlib.h>
#include <string.h>

#include <locale.h>
#include <errno.h>

#ifdef HAVE_LIBUNISTRING
# include <unistdio.h>
# define ulc_printf(...) ulc_fprintf(stdout,__VA_ARGS__)
#else
# define ulc_printf printf
#endif

/* for errors */
static GQuark LXKEYS_ERROR;
typedef enum {
    LXKEYS_BAD_ARGS, /* invalid commandline arguments */
    LXKEYS_NOT_SUPPORTED /* operation not supported */
} LuxeHotkeyError;

/* simple functions to manage LuxeHotkeyAttr data type */
static inline LuxeHotkeyAttr *luxehotkey_attr_new(void)
{
    return g_slice_new0(LuxeHotkeyAttr);
}

#define free_actions(acts) g_list_free_full(acts, (GDestroyNotify)lxkeys_attr_free)

static void lxkeys_attr_free(LuxeHotkeyAttr *data)
{
    g_free(data->name);
    g_list_free_full(data->values, g_free);
    free_actions(data->subopts);
    g_free(data->desc);
    g_slice_free(LuxeHotkeyAttr, data);
}

#define NONULL(a) (a) ? ((char *)a) : ""

/* this function is taken from wmctrl utility */
#define MAX_PROPERTY_VALUE_LEN 4096

static gchar *get_property (Display *disp, Window win, /*{{{*/
        Atom xa_prop_type, gchar *prop_name, unsigned long *size)
{
    Atom xa_prop_name;
    Atom xa_ret_type;
    int ret_format;
    unsigned long ret_nitems;
    unsigned long ret_bytes_after;
    unsigned long tmp_size;
    unsigned char *ret_prop;
    gchar *ret;

    xa_prop_name = XInternAtom(disp, prop_name, False);

    /* MAX_PROPERTY_VALUE_LEN / 4 explanation (XGetWindowProperty manpage):
     *
     * long_length = Specifies the length in 32-bit multiples of the
     *               data to be retrieved.
     *
     * NOTE:  see
     * http://mail.gnome.org/archives/wm-spec-list/2003-March/msg00067.html
     * In particular:
     *
     * When the X window system was ported to 64-bit architectures, a
     * rather peculiar design decision was made. 32-bit quantities such
     * as Window IDs, atoms, etc, were kept as longs in the client side
     * APIs, even when long was changed to 64 bits.
     *
     */
    if (XGetWindowProperty(disp, win, xa_prop_name, 0, MAX_PROPERTY_VALUE_LEN / 4, False,
            xa_prop_type, &xa_ret_type, &ret_format,
            &ret_nitems, &ret_bytes_after, &ret_prop) != Success) {
        return NULL;
    }

    if (xa_ret_type != xa_prop_type || ret_nitems == 0 ||
        ((xa_prop_type == XA_WINDOW || xa_prop_type == XA_CARDINAL) && ret_format != 32)) {
        XFree(ret_prop);
        return NULL;
    }

    /* null terminate the result to make string handling easier */
    tmp_size = (ret_format / 8) * ret_nitems;
    /* Correct 64 Architecture implementation of 32 bit data */
    if(ret_format==32) tmp_size *= sizeof(long)/4;
    ret = g_malloc(tmp_size + 1);
    memcpy(ret, ret_prop, tmp_size);
    ret[tmp_size] = '\0';

    if (size) {
        *size = tmp_size;
    }

    XFree(ret_prop);
    return ret;
} /*}}}*/

static gchar *get_wm_info(void)
{
    /* this code is taken from wmctrl utility, adapted
       Copyright (C) 2003, Tomas Styblo <tripie@cpan.org> */
    Display *disp;
    Window *sup_window = NULL;
    gchar *wm_name = NULL;

    if (!(disp = XOpenDisplay(NULL))) {
        fputs("Cannot open display.\n", stderr);
        return NULL;
    }

    if (!(sup_window = (Window *)get_property(disp, DefaultRootWindow(disp),
                    XA_WINDOW, "_NET_SUPPORTING_WM_CHECK", NULL))) {
        if (!(sup_window = (Window *)get_property(disp, DefaultRootWindow(disp),
                        XA_CARDINAL, "_WIN_SUPPORTING_WM_CHECK", NULL))) {
            fputs("Cannot get window manager info properties.\n"
                  "(_NET_SUPPORTING_WM_CHECK or _WIN_SUPPORTING_WM_CHECK)\n", stderr);
            XCloseDisplay(disp);
            return NULL;
        }
    }

    /* WM_NAME */
    if (!(wm_name = get_property(disp, *sup_window,
            XInternAtom(disp, "UTF8_STRING", False), "_NET_WM_NAME", NULL))) {
        if (!(wm_name = get_property(disp, *sup_window,
                XA_STRING, "_NET_WM_NAME", NULL))) {
            fputs("Cannot get name of the window manager (_NET_WM_NAME).\n", stderr);
        } else {
            gchar *_wm_name = wm_name;

            wm_name = g_locale_to_utf8(_wm_name, -1, NULL, NULL, NULL);
            if (wm_name)
                g_free(_wm_name);
            else
                /* Cannot convert string from locale charset to UTF-8. */
                wm_name = _wm_name;
        }
    }

    g_free(sup_window);
    XCloseDisplay(disp);
    return wm_name;
}

/* test if we are called from X which is local */
static gboolean test_X_is_local(void)
{
    const char *display = g_getenv("DISPLAY");
    int Xnum;
    char lockfile[32];

    if (display)
        display = strchr(display, ':');
    if (!display || display[1] < '0' || display[1] > '9')
        /* invalid environment */
        return FALSE;
    Xnum = atoi(&display[1]);
    snprintf(lockfile, sizeof(lockfile), "/tmp/.X%d-lock", Xnum);
    if (g_file_test(lockfile, G_FILE_TEST_IS_REGULAR))
        return TRUE;
    /* Nested servers (Xephyr -displayfd) create only the local socket. */
    snprintf(lockfile, sizeof(lockfile), "/tmp/.X11-unix/X%d", Xnum);
    return g_file_test(lockfile, G_FILE_TEST_EXISTS);
}


/* WM plugins */
typedef struct LuxeHotkeyPlugin {
    struct LuxeHotkeyPlugin *next;
    gchar *name;
    LuxeHotkeyPluginInit *t;
} LuxeHotkeyPlugin;

static LuxeHotkeyPlugin *plugins = NULL;

FM_MODULE_DEFINE_TYPE(luxehotkey, LuxeHotkeyPluginInit, 1)
static gboolean fm_module_callback_luxehotkey(const char *name, gpointer init, int ver)
{
    LuxeHotkeyPlugin *plugin;

    /* ignore ver for now, only 1 exists */
    /* FIXME: need to check for duplicates? */
    plugin = g_new(LuxeHotkeyPlugin, 1);
    plugin->next = plugins;
    plugin->name = g_strdup(name);
    plugin->t = init;
    plugins = plugin;
    return TRUE;
}


/* GUI plugins */
typedef struct LuxeHotkeyGUIPlugin {
    struct LuxeHotkeyGUIPlugin *next;
    gchar *name;
    LuxeHotkeyGUIPluginInit *t;
} LuxeHotkeyGUIPlugin;

static LuxeHotkeyGUIPlugin *gui_plugins = NULL;

FM_MODULE_DEFINE_TYPE(luxehotkey_gui, LuxeHotkeyGUIPluginInit, 1)
static gboolean fm_module_callback_luxehotkey_gui(const char *name, gpointer init, int ver)
{
    LuxeHotkeyGUIPlugin *plugin;

    /* ignore ver for now, only 1 exists */
    /* FIXME: need to check for duplicates? */
    plugin = g_new(LuxeHotkeyGUIPlugin, 1);
    plugin->next = gui_plugins;
    plugin->name = g_strdup(name);
    plugin->t = init;
    gui_plugins = plugin;
    return TRUE;
}


static int _print_help(const char *cmd)
{
    printf(_("Usage: %s global [<action>]      - show keys bound to action(s)\n"), cmd);
    printf(_("       %s global <action> <key>  - bind a key to the action\n"), cmd);
    printf(_("       %s app [<exec>]           - show keys bound to exec line\n"), cmd);
    printf(_("       %s app <exec> <key>       - bind a key to some exec line\n"), cmd);
    printf(_("       %s app <exec> <key> --options <options> - set app options\n"), cmd);
    printf(_("       %s app <exec> --          - unbind all keys from exec line\n"), cmd);
    printf(_("       Use \\& for a literal ampersand in legacy EXEC&OPTIONS syntax.\n"));
    printf(_("       %s show <key>             - show the action bound to a key\n"), cmd);
    printf(_("       %s --gui=<type>           - start with GUI\n"), cmd);
    return 0;
}

/* convert text line to LuxeHotkeyAttr list
   text may be formatted like this:
   startupnotify=yes:attr1=val1:attr2=val2&action=val
   any ':','&','\' in any value should be escaped with '\' */
static GList *actions_from_str(const char *line, GError **error)
{
    GString *str = g_string_sized_new(16);
    LuxeHotkeyAttr *data = NULL, *attr = NULL;
    GList *list;

    data = luxehotkey_attr_new();
    list = g_list_prepend(NULL, data);
    for (; *line; line++) {
        switch (*line) {
        case '=':
            if (!data->name) { /* this is new data value */
                if (str->len == 0)
                    goto _empty_name;
                data->name = g_strdup(str->str);
                g_string_truncate(str, 0);
            } else if (attr && !attr->name) { /* this is an option */
                if (str->len == 0)
                    goto _empty_opt;
                attr->name = g_strdup(str->str);
                g_string_truncate(str, 0);
            } else /* '=' in value, continue processing */
                g_string_append_c(str, *line);
            break;
        case ':':
            if (!data->name) {
                if (str->len == 0) /* empty action name */
                    goto _empty_name;
                data->name = g_strdup(str->str);
            } else if (attr) { /* finish previous attr */
                if (!attr->name) {
                    if (str->len == 0)
                        goto _empty_opt;
                    attr->name = g_strdup(str->str);
                } else
                    attr->values = g_list_prepend(NULL, g_strdup(str->str));
            } else /* got value for the action */
                data->values = g_list_prepend(NULL, g_strdup(str->str));
            g_string_truncate(str, 0);
            attr = luxehotkey_attr_new();
            data->subopts = g_list_prepend(data->subopts, attr);
            break;
        case '&':
            if (!data->name) {
                if (str->len == 0) /* empty action name */
                    goto _empty_name;
                data->name = g_strdup(str->str);
            } else if (attr) { /* finish last attr */
                if (!attr->name) {
                    if (str->len == 0)
                        goto _empty_opt;
                    attr->name = g_strdup(str->str);
                } else
                    attr->values = g_list_prepend(NULL, g_strdup(str->str));
            } else /* got value for the action */
                data->values = g_list_prepend(NULL, g_strdup(str->str));
            g_string_truncate(str, 0);
            attr = NULL; /* previous action just finished */
            data->subopts = g_list_reverse(data->subopts);
            data = luxehotkey_attr_new();
            list = g_list_prepend(list, data);
            break;
        case '\\':
            /* escape char: take the next char literally so that escaped
               ':', '&', '=' and literal '\\' survive the round-trip */
            if (line[1] != '\0')
                g_string_append_c(str, *++line);
            else /* trailing backslash, keep it */
                g_string_append_c(str, '\\');
            break;
        default:
            g_string_append_c(str, *line);
        }
    }
    if (!data->name) {
        if (str->len == 0) /* empty action name */
            goto _empty_name;
        data->name = g_strdup(str->str);
    } else if (attr) { /* finish last attr */
        if (!attr->name) {
            if (str->len == 0)
                goto _empty_opt;
            attr->name = g_strdup(str->str);
        } else
            attr->values = g_list_prepend(NULL, g_strdup(str->str));
    } else /* got value for the action */
        data->values = g_list_prepend(NULL, g_strdup(str->str));
    data->subopts = g_list_reverse(data->subopts);
    list = g_list_reverse(list);
    g_string_free(str, TRUE);
    return list;

_empty_opt:
    g_set_error_literal(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS, _("empty option name."));
    goto _failed;
_empty_name:
    g_set_error_literal(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS, _("empty action name."));
_failed:
    free_actions(list);
    g_string_free(str, TRUE);
    return NULL;
}

/* check if action list matches origin
   if origin==NULL then error is possibly set already */
static gboolean numeric_value_matches(const char *value, const char *kind)
{
    char *end;
    if (strcmp(kind, "#") != 0 && strcmp(kind, "%") != 0)
        return FALSE;
    errno = 0;
    g_ascii_strtoll(value, &end, 10);
    return errno != ERANGE && end != value &&
           (kind[0] == '#' ? *end == '\0' : strcmp(end, "%") == 0);
}

static gboolean validate_actions(const GList *act, const GList *origin,
                                 const LuxeHotkeyAttr *action, gchar *wm_name,
                                 GError **error)
{
    const LuxeHotkeyAttr *data, *ordata;
    const GList *l, *olist;

    if (!origin)
        return FALSE;
    if (action)
        olist = action->subopts; /* action is ordata on recursion actually */
    else
        olist = origin;
    while (act) {
        data = act->data;
        /* find corresponding descriptor in the origin list */
        for (l = olist; l; l = l->next)
            if (g_strcmp0(data->name, (ordata = l->data)->name) == 0)
                break;
        if (l == NULL) {
            if (action)
                g_set_error(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                            _("no matching option '%s' found for action '%s'."),
                            data->name, action->name);
            else
                g_set_error(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                            _("action '%s' isn't supported by WM %s."),
                            data->name, wm_name);
            return FALSE;
        }
        /* if ordata->values isn't NULL and data->values isn't NULL
           then it must match, ordata->values==NULL means anything matches */
        if (data->values != NULL && ordata->values != NULL) {
            for (l = ordata->values; l; l = l->next)
                if (g_strcmp0(data->values->data, l->data) == 0 ||
                    /* check for numeric value too */
                    numeric_value_matches(data->values->data, l->data))
                    break;
            if (l == NULL) {
                if (action)
                    g_set_error(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                                _("value '%s' is not supported for option '%s'."),
                                (char *)data->values->data, data->name);
                else
                    g_set_error(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                                _("value '%s' is not supported for action '%s'."),
                                (char *)data->values->data, data->name);
                return FALSE;
            }
        }
        /* for each data->subopts do recursion against ordata->subopts */
        if (!data->subopts) ;
        else if (ordata->has_actions) {
            /* test against origin actions list, not suboptions */
            if (!validate_actions(data->subopts, origin, NULL, wm_name, error))
                return FALSE;
        } else if (!ordata->subopts) {
            g_set_error(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                        _("action '%s' does not support options."), data->name);
            return FALSE;
        } else if (!validate_actions(data->subopts, origin, ordata, wm_name, error))
            return FALSE;
        act = act->next;
    }
    return TRUE;
}

/* escape '\\', ':', '&' and '=' with '\\' so actions_from_str() restores them */
static void actions_escape_into(GString *str, const char *value)
{
    for (; *value; value++) {
        if (*value == '\\' || *value == ':' || *value == '&' || *value == '=')
            g_string_append_c(str, '\\');
        g_string_append_c(str, *value);
    }
}

/* convert list to a text line, inverse of actions_from_str();
 * only the round-trip test (which includes this file) calls it */
G_GNUC_UNUSED static char *actions_to_str(const GList *act)
{
    GString *str = g_string_sized_new(64);
    gboolean first_action = TRUE;

    for (; act; act = act->next) {
        const LuxeHotkeyAttr *data = act->data;
        const GList *l;

        if (!first_action)
            g_string_append_c(str, '&');
        first_action = FALSE;
        actions_escape_into(str, NONULL(data->name));
        if (data->values && data->values->data) {
            g_string_append_c(str, '=');
            actions_escape_into(str, data->values->data);
        }
        for (l = data->subopts; l; l = l->next) {
            const LuxeHotkeyAttr *attr = l->data;

            g_string_append_c(str, ':');
            actions_escape_into(str, NONULL(attr->name));
            if (attr->values && attr->values->data) {
                g_string_append_c(str, '=');
                actions_escape_into(str, attr->values->data);
            }
        }
    }
    return g_string_free(str, FALSE);
}

/* Validate the same modifier and keysym grammar used by the Openbox writer. */
static gboolean validate_key(const char *key, GError **error)
{
    if (!luxe_shortcut_valid(key)) {
        g_set_error(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                    _("invalid keybinding '%s'."), key ? key : "");
        return FALSE;
    }
    return TRUE;
}

/* Minimal exec-line validation: the command must be present and non-blank. */
static gboolean validate_exec(const char *exec, GError **error)
{
    char *copy;
    gboolean ok;

    if (exec == NULL) {
        g_set_error_literal(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                            _("The exec line cannot be empty."));
        return FALSE;
    }
    copy = g_strdup(exec);
    ok = g_strstrip(copy)[0] != '\0';
    g_free(copy);
    if (!ok)
        g_set_error_literal(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                            _("The exec line cannot be empty."));
    return ok;
}

static const char *find_legacy_options_separator(const char *spec)
{
    for (const char *p = spec; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++; /* An escaped ampersand belongs to the command, not options. */
            continue;
        }
        if (*p == '&')
            return p;
    }
    return NULL;
}

static char *unescape_command_ampersands(const char *command, gsize length)
{
    GString *result = g_string_sized_new(length);
    for (gsize i = 0; i < length; i++) {
        if (command[i] == '\\' && i + 1 < length && command[i + 1] == '&')
            i++;
        g_string_append_c(result, command[i]);
    }
    return g_string_free(result, FALSE);
}

/* New callers can pass options separately. The legacy command&options form
 * remains supported, with \& representing a literal ampersand in EXEC. */
static gboolean parse_app_spec(const char *spec, const char *explicit_options,
                               char **exec, GList **options, GError **error)
{
    const char *separator = explicit_options ? NULL : find_legacy_options_separator(spec);
    *exec = explicit_options ? g_strdup(spec)
        : unescape_command_ampersands(spec, separator ? separator - spec : strlen(spec));
    *options = NULL;
    const char *option_text = explicit_options ? explicit_options
                              : separator ? separator + 1 : NULL;
    if (option_text) {
        *options = actions_from_str(option_text, error);
        if (!*options)
            goto fail;
    }
    if (!validate_exec(*exec, error))
        goto fail;
    return TRUE;

fail:
    free_actions(*options);
    *options = NULL;
    g_clear_pointer(exec, g_free);
    return FALSE;
}

static void print_suboptions(GList *sub, int indent)
{
    indent += 3;
    while (sub) {
        LuxeHotkeyAttr *action = sub->data;
        if (action->values && action->values->data)
            printf("%*s%s=%s\n", indent, "", action->name,
                                 (char *)action->values->data);
        else
            printf("%*s%s\n", indent, "", action->name);
        print_suboptions(action->subopts, indent);
        sub = sub->next;
    }
}


int main(int argc, char *argv[])
{
    const char *cmd;
    gchar *wm_name = NULL;
    LuxeHotkeyPlugin *plugin = NULL;
    LuxeHotkeyGUIPlugin *gui_plugin = NULL;
    int ret = 1; /* failure */
    gpointer config = NULL;
    GError *error = NULL;
    gboolean do_gui = FALSE;

    /* init localizations */
#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    setlocale(LC_MESSAGES, "");
#endif

    /* parse args first, show help if "help" "-h" or "--help" */
    if (argc < 2)
        cmd = "--gui=gtk";
    else
        cmd = argv[1];
    if (cmd[0] == '-' && cmd[1] == '-') /* skip leading "--" if given */
        cmd += 2;
    if (strcmp(cmd, "help") == 0 || (cmd[0] == '-' && cmd[1] == 'h'))
        return _print_help(argv[0]);
    if (strncmp(cmd, "gui=", 4) == 0) {
        do_gui = TRUE;
        cmd += 4;
        luxe_ui_init();
    }

    /* init LibFM and FmModule */
    fm_init(NULL);
    fm_modules_add_directory(PACKAGE_PLUGINS_DIR);
    fm_module_register_luxehotkey();
    if (do_gui)
        fm_module_register_luxehotkey_gui();

    LXKEYS_ERROR = g_quark_from_static_string("luxehotkey-error");

    CHECK_MODULES();
    if (do_gui) /* load GUI plugin if requested */
        for (gui_plugin = gui_plugins; gui_plugin; gui_plugin = gui_plugin->next)
            if (g_ascii_strcasecmp(gui_plugin->name, cmd) == 0)
                break;
    /* initialize GUI before error may be reported */
    if (gui_plugin && gui_plugin->t->init)
        gui_plugin->t->init(argc, argv);
    if (!test_X_is_local()) {
        g_set_error_literal(&error, LXKEYS_ERROR, LXKEYS_NOT_SUPPORTED,
                            _("Sorry, cannot configure keys remotely."));
        goto _exit;
    }
    /* detect current WM and find a module for it */
    wm_name = get_wm_info();
    if (!wm_name)
    {
        g_set_error_literal(&error, LXKEYS_ERROR, LXKEYS_NOT_SUPPORTED,
                            _("Could not determine window manager running."));
        goto _exit;
    }
    for (plugin = plugins; plugin; plugin = plugin->next)
        if (g_ascii_strcasecmp(plugin->name, wm_name) == 0)
            break;
    if (!plugin) {
        g_set_error(&error, LXKEYS_ERROR, LXKEYS_NOT_SUPPORTED,
                    _("Window manager %s isn't supported now, sorry."), wm_name);
        goto _exit;
    }

    /* load the found module */
    config = plugin->t->load(NULL, &error);
    if (!config) {
        g_prefix_error(&error, _("Problems loading configuration: "));
        goto _exit;
    }

    if (do_gui) {
        if (gui_plugin && gui_plugin->t->run)
            gui_plugin->t->run(wm_name, plugin->t, &config, &error);
        else
            g_set_error(&error, LXKEYS_ERROR, LXKEYS_NOT_SUPPORTED,
                        _("GUI type %s currently isn't supported."), cmd);
        goto _exit;
    }

    /* doing commandline: call module function depending on args */
    if (strcmp(cmd, "global") == 0) { /* luxehotkey global ... */
        if (argc > 3) { /* set */
            LuxeHotkeyGlobal data;

            if (plugin->t->set_wm_key == NULL)
                goto _not_supported;
            /* parse and validate actions */
            data.actions = actions_from_str(argv[2], &error);
            if (error ||
                (plugin->t->get_wm_actions != NULL &&
                 !validate_actions(data.actions, plugin->t->get_wm_actions(config, &error),
                                   NULL, wm_name, &error))) { /* invalid request */
                g_prefix_error(&error, _("Invalid request: "));
                free_actions(data.actions);
                goto _exit;
            }
            if (!validate_key(argv[3], &error) ||
                (argc > 4 && !validate_key(argv[4], &error))) {
                g_prefix_error(&error, _("Invalid request: "));
                free_actions(data.actions);
                goto _exit;
            }
            data.accel1 = argv[3];
            data.accel2 = NULL;
            if (argc > 4)
                data.accel2 = argv[4];
            if (!plugin->t->set_wm_key(config, &data, &error) ||
                !plugin->t->save(config, &error)) {
                g_prefix_error(&error, _("Problems saving configuration: "));
                free_actions(data.actions);
                goto _exit;
            }
            free_actions(data.actions);
        } else { /* show by mask */
            const char *mask = NULL;
            GList *keys, *key;
            LuxeHotkeyGlobal *data;
            GList *act;
            LuxeHotkeyAttr *action;

            if (plugin->t->get_wm_keys == NULL)
                goto _not_supported;
            if (argc > 2)
                mask = argv[2]; /* mask given */
            keys = plugin->t->get_wm_keys(config, mask, NULL);
            ulc_printf(" %-24s %s\n", _("ACTION(s)"), _("KEY(s)"));
            for (key = keys; key; key = key->next) {
                data = key->data;
                for (act = data->actions; act; act = act->next)
                {
                    action = act->data;
                    if (act != data->actions)
                        printf("+ %s\n", action->name);
                    else if (data->accel2)
                        ulc_printf("%-24s %s %s\n", action->name, data->accel1,
                                                    data->accel2);
                    else
                        ulc_printf("%-24s %s\n", action->name, data->accel1);
                    print_suboptions(action->subopts, 0);
                }
            }
            g_list_free(keys);
        }
    } else if (strcmp(cmd, "app") == 0) { /* luxehotkey app ... */
        if (argc > 3) { /* set */
            GList *keys = NULL;
            LuxeHotkeyApp data;
            const char *key2 = NULL;
            const char *explicit_options = NULL;

            if (plugin->t->set_app_key == NULL)
                goto _not_supported;
            if (argc > 4) {
                if (g_strcmp0(argv[4], "--options") == 0 && argc == 6)
                    explicit_options = argv[5];
                else if (argc == 5)
                    key2 = argv[4];
                else if (argc == 7 && g_strcmp0(argv[5], "--options") == 0) {
                    key2 = argv[4];
                    explicit_options = argv[6];
                } else {
                    g_set_error_literal(&error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                        _("Use app EXEC KEY [KEY2] [--options OPTIONS]."));
                    goto _exit;
                }
            }
            if (explicit_options && key2 == NULL && argc != 6) {
                g_set_error_literal(&error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                    _("Use app EXEC KEY [KEY2] [--options OPTIONS]."));
                goto _exit;
            }
            if (key2 && (g_strcmp0(argv[3], "--") == 0 || !validate_key(key2, &error))) {
                if (!error)
                    g_set_error_literal(&error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                                        _("A second key requires a first key."));
                g_prefix_error(&error, _("Invalid request: "));
                goto _exit;
            }
            if (g_strcmp0(argv[3], "--") != 0 && !validate_key(argv[3], &error)) {
                g_prefix_error(&error, _("Invalid request: "));
                goto _exit;
            }
            if (!parse_app_spec(argv[2], explicit_options, &data.exec,
                                &data.options, &error)) {
                g_prefix_error(&error, _("Invalid request: "));
                goto _exit;
            }
            if (data.options && plugin->t->get_app_options != NULL &&
                !validate_actions(data.options,
                                  plugin->t->get_app_options(config, &error),
                                  NULL, wm_name, &error)) {
                g_prefix_error(&error, _("Invalid request: "));
                free_actions(data.options);
                g_free(data.exec);
                goto _exit;
            }
            data.accel1 = NULL;
            data.accel2 = NULL;
            if (g_strcmp0(argv[3], "--") == 0) {
                data.accel1 = NULL; /* remove all bindings */
            } else if (key2) {
                data.accel1 = argv[3];
                data.accel2 = (char *)key2;
            } else {
                /* A single key adds/replaces the alternate binding when one
                 * exists; otherwise it establishes the primary binding. */
                if (plugin->t->get_app_keys)
                    keys = plugin->t->get_app_keys(config, data.exec, NULL);
                if (keys && keys->next) {
                    g_set_error_literal(&error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                                        _("The command matches multiple application bindings."));
                    g_list_free(keys);
                    free_actions(data.options);
                    g_free(data.exec);
                    goto _exit;
                }
                if (keys && ((LuxeHotkeyApp *)keys->data)->accel1) {
                    data.accel1 = ((LuxeHotkeyApp *)keys->data)->accel1;
                    data.accel2 = argv[3];
                } else {
                    data.accel1 = argv[3];
                }
                g_list_free(keys);
                keys = NULL;
            }
            /* check if exec already has a key */
            if (!plugin->t->set_app_key(config, &data, &error) ||
                !plugin->t->save(config, &error)) {
                g_prefix_error(&error, _("Problems saving configuration: "));
                free_actions(data.options);
                g_free(data.exec);
                goto _exit;
            }
            free_actions(data.options);
            g_free(data.exec);
        } else { /* show by mask */
            const char *mask = NULL;
            GList *keys, *key;
            LuxeHotkeyApp *data;

            if (plugin->t->get_app_keys == NULL)
                goto _not_supported;
            if (argc > 2)
                mask = argv[2]; /* mask given */
            keys = plugin->t->get_app_keys(config, mask, NULL);
            ulc_printf(" %-48s %s\n", _("EXEC"), _("KEY(s)"));
            for (key = keys; key; key = key->next) {
                data = key->data;
                if (data->accel2)
                    ulc_printf("%-48s %s %s\n", data->exec, data->accel1,
                                                data->accel2);
                else
                    ulc_printf("%-48s %s\n", data->exec, data->accel1);
                print_suboptions(data->options, 0);
            }
            g_list_free(keys);
        }
    } else if (strcmp(cmd, "show") == 0) { /* luxehotkey show <key> */
        GList *keys, *key;
        gboolean found = FALSE;

        if (plugin->t->get_wm_keys == NULL && plugin->t->get_app_keys == NULL)
            goto _not_supported;
        if (argc != 3) {
            g_set_error_literal(&error, LXKEYS_ERROR, LXKEYS_BAD_ARGS,
                                _("Usage: luxehotkey show <key>"));
            goto _exit;
        }
        if (!validate_key(argv[2], &error)) {
            g_prefix_error(&error, _("Invalid request: "));
            goto _exit;
        }
        if (plugin->t->get_wm_keys != NULL) {
            keys = plugin->t->get_wm_keys(config, NULL, NULL);
            for (key = keys; key; key = key->next) {
                LuxeHotkeyGlobal *data = key->data;

                if (g_strcmp0(argv[2], data->accel1) == 0 ||
                    g_strcmp0(argv[2], data->accel2) == 0) {
                    GList *act;

                    for (act = data->actions; act; act = act->next) {
                        LuxeHotkeyAttr *action = act->data;

                        if (act != data->actions)
                            printf("+ %s\n", action->name);
                        else
                            ulc_printf("%-24s %s\n", action->name, argv[2]);
                        print_suboptions(action->subopts, 0);
                    }
                    found = TRUE;
                }
            }
            g_list_free(keys);
        }
        if (plugin->t->get_app_keys != NULL) {
            keys = plugin->t->get_app_keys(config, NULL, NULL);
            for (key = keys; key; key = key->next) {
                LuxeHotkeyApp *data = key->data;

                if (g_strcmp0(argv[2], data->accel1) == 0 ||
                    g_strcmp0(argv[2], data->accel2) == 0) {
                    ulc_printf("%-48s %s\n", data->exec, argv[2]);
                    print_suboptions(data->options, 0);
                    found = TRUE;
                }
            }
            g_list_free(keys);
        }
        if (!found)
            printf(_("No action bound to key '%s'.\n"), argv[2]);
    } else
        goto _exit;
    ret = 0; /* success */
    goto _exit;

_not_supported:
    g_set_error_literal(&error, LXKEYS_ERROR, LXKEYS_NOT_SUPPORTED,
                        _("Requested operation isn't supported."));

    /* release resources */
_exit:
    if (config)
        plugin->t->free(config);
    if (error) {
        /* if do_gui then show an alert window instead of stderr */
        if (gui_plugin && gui_plugin->t->alert)
            gui_plugin->t->alert(error);
        else
            fprintf(stderr, "LuxeHotkey: %s\n", error->message);
        g_error_free(error);
    }
    fm_module_unregister_type("luxehotkey");
    if (do_gui)
        fm_module_unregister_type("luxehotkey_gui");
    while (plugins) {
        plugin = plugins;
        plugins = plugin->next;
        g_free(plugin->name);
        g_free(plugin);
    }
    while (gui_plugins) {
        gui_plugin = gui_plugins;
        gui_plugins = gui_plugin->next;
        g_free(gui_plugin->name);
        g_free(gui_plugin);
    }
    g_free(wm_name);

    return ret;
}
