/* Maintained C build source; see MAINTAINERS.md for adjacent Vala references. */

/* 
 *      Copyright 2011 Julien Lavergne <gilir@ubuntu.com>
 *      Copyright (C) 2026 Jerry Howell
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
*/
/* 
   TODO packagekit handler (GUI and stuff) ?
   TODO Use wnck for managing launching applications ?
*/

#include <glib-object.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <gio/gio.h>
#include <glib/gi18n-lib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "safe-file.h"
#include "supervision-policy.h"

void lxsession_luxesession_spawn_command_line_async(const gchar *command_line);

#if !defined(VALA_STRICT_C)
#if !defined(__clang__) && defined(__GNUC__) && (__GNUC__ >= 14)
#pragma GCC diagnostic warning "-Wincompatible-pointer-types"
#elif defined(__clang__) && (__clang_major__ >= 16)
#pragma clang diagnostic ignored "-Wincompatible-function-pointer-types"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#endif
#endif
#if !defined(VALA_EXTERN)
#if defined(_MSC_VER)
#define VALA_EXTERN __declspec(dllexport) extern
#elif __GNUC__ >= 4
#define VALA_EXTERN __attribute__((visibility("default"))) extern
#else
#define VALA_EXTERN extern
#endif
#endif

static gboolean
lxsession_parse_timeout (const gchar *text, gint *value)
{
        gchar *end = NULL;
        gint64 parsed;
        if (text == NULL || value == NULL || *text == '\0')
                return FALSE;
        parsed = g_ascii_strtoll(text, &end, 10);
        if (end == text || *end != '\0' || parsed < 1 || parsed > 86400)
                return FALSE;
        *value = (gint)parsed;
        return TRUE;
}

static gboolean
lxsession_parse_count (const gchar *text, gint *value)
{
        gchar *end = NULL;
        gint64 parsed;
        if (text == NULL || value == NULL || *text == '\0')
                return FALSE;
        parsed = g_ascii_strtoll(text, &end, 10);
        if (end == text || *end != '\0' || parsed < 0 || parsed > G_MAXINT)
                return FALSE;
        *value = (gint)parsed;
        return TRUE;
}

#define LXSESSION_TYPE_APP_OBJECT (lxsession_app_object_get_type ())
#define LXSESSION_APP_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_APP_OBJECT, LxsessionAppObject))
#define LXSESSION_APP_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_APP_OBJECT, LxsessionAppObjectClass))
#define LXSESSION_IS_APP_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_APP_OBJECT))
#define LXSESSION_IS_APP_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_APP_OBJECT))
#define LXSESSION_APP_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_APP_OBJECT, LxsessionAppObjectClass))

typedef struct _LxsessionAppObject LxsessionAppObject;
typedef struct _LxsessionAppObjectClass LxsessionAppObjectClass;
typedef struct _LxsessionAppObjectPrivate LxsessionAppObjectPrivate;
enum  {
	LXSESSION_APP_OBJECT_0_PROPERTY,
	LXSESSION_APP_OBJECT_NAME_PROPERTY,
	LXSESSION_APP_OBJECT_COMMAND_PROPERTY,
	LXSESSION_APP_OBJECT_GUARD_PROPERTY,
	LXSESSION_APP_OBJECT_APPLICATION_TYPE_PROPERTY,
	LXSESSION_APP_OBJECT_CRASH_COUNT_PROPERTY,
	LXSESSION_APP_OBJECT_STOP_RELOAD_PROPERTY,
	LXSESSION_APP_OBJECT_NUM_PROPERTIES
};
static GParamSpec* lxsession_app_object_properties[LXSESSION_APP_OBJECT_NUM_PROPERTIES];
#define _g_free0(var) (var = (g_free (var), NULL))
#define _g_error_free0(var) ((var == NULL) ? NULL : (var = (g_error_free (var), NULL)))

#define LXSESSION_TYPE_SIMPLE_APP_OBJECT (lxsession_simple_app_object_get_type ())
#define LXSESSION_SIMPLE_APP_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_SIMPLE_APP_OBJECT, LxsessionSimpleAppObject))
#define LXSESSION_SIMPLE_APP_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_SIMPLE_APP_OBJECT, LxsessionSimpleAppObjectClass))
#define LXSESSION_IS_SIMPLE_APP_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_SIMPLE_APP_OBJECT))
#define LXSESSION_IS_SIMPLE_APP_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_SIMPLE_APP_OBJECT))
#define LXSESSION_SIMPLE_APP_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_SIMPLE_APP_OBJECT, LxsessionSimpleAppObjectClass))

typedef struct _LxsessionSimpleAppObject LxsessionSimpleAppObject;
typedef struct _LxsessionSimpleAppObjectClass LxsessionSimpleAppObjectClass;
typedef struct _LxsessionSimpleAppObjectPrivate LxsessionSimpleAppObjectPrivate;
enum  {
	LXSESSION_SIMPLE_APP_OBJECT_0_PROPERTY,
	LXSESSION_SIMPLE_APP_OBJECT_NUM_PROPERTIES
};
static GParamSpec* lxsession_simple_app_object_properties[LXSESSION_SIMPLE_APP_OBJECT_NUM_PROPERTIES];

#define LXSESSION_TYPE_GENERIC_APP_OBJECT (lxsession_generic_app_object_get_type ())
#define LXSESSION_GENERIC_APP_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_GENERIC_APP_OBJECT, LxsessionGenericAppObject))
#define LXSESSION_GENERIC_APP_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_GENERIC_APP_OBJECT, LxsessionGenericAppObjectClass))
#define LXSESSION_IS_GENERIC_APP_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_GENERIC_APP_OBJECT))
#define LXSESSION_IS_GENERIC_APP_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_GENERIC_APP_OBJECT))
#define LXSESSION_GENERIC_APP_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_GENERIC_APP_OBJECT, LxsessionGenericAppObjectClass))

typedef struct _LxsessionGenericAppObject LxsessionGenericAppObject;
typedef struct _LxsessionGenericAppObjectClass LxsessionGenericAppObjectClass;
typedef struct _LxsessionGenericAppObjectPrivate LxsessionGenericAppObjectPrivate;
enum  {
	LXSESSION_GENERIC_APP_OBJECT_0_PROPERTY,
	LXSESSION_GENERIC_APP_OBJECT_NUM_PROPERTIES
};
static GParamSpec* lxsession_generic_app_object_properties[LXSESSION_GENERIC_APP_OBJECT_NUM_PROPERTIES];

#define LXSESSION_TYPE_APP_TYPE (lxsession_app_type_get_type ())
typedef struct _LxsessionAppType LxsessionAppType;

#define LXSESSION_TYPE_GENERIC_SIMPLE_APP (lxsession_generic_simple_app_get_type ())
#define LXSESSION_GENERIC_SIMPLE_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_GENERIC_SIMPLE_APP, LxsessionGenericSimpleApp))
#define LXSESSION_GENERIC_SIMPLE_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_GENERIC_SIMPLE_APP, LxsessionGenericSimpleAppClass))
#define LXSESSION_IS_GENERIC_SIMPLE_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_GENERIC_SIMPLE_APP))
#define LXSESSION_IS_GENERIC_SIMPLE_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_GENERIC_SIMPLE_APP))
#define LXSESSION_GENERIC_SIMPLE_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_GENERIC_SIMPLE_APP, LxsessionGenericSimpleAppClass))

typedef struct _LxsessionGenericSimpleApp LxsessionGenericSimpleApp;
typedef struct _LxsessionGenericSimpleAppClass LxsessionGenericSimpleAppClass;
typedef struct _LxsessionGenericSimpleAppPrivate LxsessionGenericSimpleAppPrivate;
enum  {
	LXSESSION_GENERIC_SIMPLE_APP_0_PROPERTY,
	LXSESSION_GENERIC_SIMPLE_APP_SETTINGS_COMMAND_PROPERTY,
	LXSESSION_GENERIC_SIMPLE_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_generic_simple_app_properties[LXSESSION_GENERIC_SIMPLE_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_WINDOWS_MANAGER_APP (lxsession_windows_manager_app_get_type ())
#define LXSESSION_WINDOWS_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_WINDOWS_MANAGER_APP, LxsessionWindowsManagerApp))
#define LXSESSION_WINDOWS_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_WINDOWS_MANAGER_APP, LxsessionWindowsManagerAppClass))
#define LXSESSION_IS_WINDOWS_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_WINDOWS_MANAGER_APP))
#define LXSESSION_IS_WINDOWS_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_WINDOWS_MANAGER_APP))
#define LXSESSION_WINDOWS_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_WINDOWS_MANAGER_APP, LxsessionWindowsManagerAppClass))

typedef struct _LxsessionWindowsManagerApp LxsessionWindowsManagerApp;
typedef struct _LxsessionWindowsManagerAppClass LxsessionWindowsManagerAppClass;
typedef struct _LxsessionWindowsManagerAppPrivate LxsessionWindowsManagerAppPrivate;
enum  {
	LXSESSION_WINDOWS_MANAGER_APP_0_PROPERTY,
	LXSESSION_WINDOWS_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_windows_manager_app_properties[LXSESSION_WINDOWS_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_LXSESSION_CONFIG (lxsession_lxsession_config_get_type ())
#define LXSESSION_LXSESSION_CONFIG(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_LXSESSION_CONFIG, LxsessionLxsessionConfig))
#define LXSESSION_LXSESSION_CONFIG_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_LXSESSION_CONFIG, LxsessionLxsessionConfigClass))
#define LXSESSION_IS_LXSESSION_CONFIG(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_LXSESSION_CONFIG))
#define LXSESSION_IS_LXSESSION_CONFIG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_LXSESSION_CONFIG))
#define LXSESSION_LXSESSION_CONFIG_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_LXSESSION_CONFIG, LxsessionLxsessionConfigClass))

typedef struct _LxsessionLxsessionConfig LxsessionLxsessionConfig;
typedef struct _LxsessionLxsessionConfigClass LxsessionLxsessionConfigClass;

#define LXSESSION_TYPE_LXSESSION_CONFIG_KEY_FILE (lxsession_lxsession_config_key_file_get_type ())
#define LXSESSION_LXSESSION_CONFIG_KEY_FILE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_LXSESSION_CONFIG_KEY_FILE, LxsessionLxsessionConfigKeyFile))
#define LXSESSION_LXSESSION_CONFIG_KEY_FILE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_LXSESSION_CONFIG_KEY_FILE, LxsessionLxsessionConfigKeyFileClass))
#define LXSESSION_IS_LXSESSION_CONFIG_KEY_FILE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_LXSESSION_CONFIG_KEY_FILE))
#define LXSESSION_IS_LXSESSION_CONFIG_KEY_FILE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_LXSESSION_CONFIG_KEY_FILE))
#define LXSESSION_LXSESSION_CONFIG_KEY_FILE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_LXSESSION_CONFIG_KEY_FILE, LxsessionLxsessionConfigKeyFileClass))

typedef struct _LxsessionLxsessionConfigKeyFile LxsessionLxsessionConfigKeyFile;
typedef struct _LxsessionLxsessionConfigKeyFileClass LxsessionLxsessionConfigKeyFileClass;
#define _g_array_unref0(var) ((var == NULL) ? NULL : (var = (g_array_unref (var), NULL)))

#define LXSESSION_TYPE_PANEL_APP (lxsession_panel_app_get_type ())
#define LXSESSION_PANEL_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_PANEL_APP, LxsessionPanelApp))
#define LXSESSION_PANEL_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_PANEL_APP, LxsessionPanelAppClass))
#define LXSESSION_IS_PANEL_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_PANEL_APP))
#define LXSESSION_IS_PANEL_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_PANEL_APP))
#define LXSESSION_PANEL_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_PANEL_APP, LxsessionPanelAppClass))

typedef struct _LxsessionPanelApp LxsessionPanelApp;
typedef struct _LxsessionPanelAppClass LxsessionPanelAppClass;
typedef struct _LxsessionPanelAppPrivate LxsessionPanelAppPrivate;
enum  {
	LXSESSION_PANEL_APP_0_PROPERTY,
	LXSESSION_PANEL_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_panel_app_properties[LXSESSION_PANEL_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_DOCK_APP (lxsession_dock_app_get_type ())
#define LXSESSION_DOCK_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_DOCK_APP, LxsessionDockApp))
#define LXSESSION_DOCK_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_DOCK_APP, LxsessionDockAppClass))
#define LXSESSION_IS_DOCK_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_DOCK_APP))
#define LXSESSION_IS_DOCK_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_DOCK_APP))
#define LXSESSION_DOCK_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_DOCK_APP, LxsessionDockAppClass))

typedef struct _LxsessionDockApp LxsessionDockApp;
typedef struct _LxsessionDockAppClass LxsessionDockAppClass;
typedef struct _LxsessionDockAppPrivate LxsessionDockAppPrivate;
enum  {
	LXSESSION_DOCK_APP_0_PROPERTY,
	LXSESSION_DOCK_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_dock_app_properties[LXSESSION_DOCK_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_SCREENSAVER_APP (lxsession_screensaver_app_get_type ())
#define LXSESSION_SCREENSAVER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_SCREENSAVER_APP, LxsessionScreensaverApp))
#define LXSESSION_SCREENSAVER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_SCREENSAVER_APP, LxsessionScreensaverAppClass))
#define LXSESSION_IS_SCREENSAVER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_SCREENSAVER_APP))
#define LXSESSION_IS_SCREENSAVER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_SCREENSAVER_APP))
#define LXSESSION_SCREENSAVER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_SCREENSAVER_APP, LxsessionScreensaverAppClass))

typedef struct _LxsessionScreensaverApp LxsessionScreensaverApp;
typedef struct _LxsessionScreensaverAppClass LxsessionScreensaverAppClass;
typedef struct _LxsessionScreensaverAppPrivate LxsessionScreensaverAppPrivate;
enum  {
	LXSESSION_SCREENSAVER_APP_0_PROPERTY,
	LXSESSION_SCREENSAVER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_screensaver_app_properties[LXSESSION_SCREENSAVER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_POWER_MANAGER_APP (lxsession_power_manager_app_get_type ())
#define LXSESSION_POWER_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_POWER_MANAGER_APP, LxsessionPowerManagerApp))
#define LXSESSION_POWER_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_POWER_MANAGER_APP, LxsessionPowerManagerAppClass))
#define LXSESSION_IS_POWER_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_POWER_MANAGER_APP))
#define LXSESSION_IS_POWER_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_POWER_MANAGER_APP))
#define LXSESSION_POWER_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_POWER_MANAGER_APP, LxsessionPowerManagerAppClass))

typedef struct _LxsessionPowerManagerApp LxsessionPowerManagerApp;
typedef struct _LxsessionPowerManagerAppClass LxsessionPowerManagerAppClass;
typedef struct _LxsessionPowerManagerAppPrivate LxsessionPowerManagerAppPrivate;
enum  {
	LXSESSION_POWER_MANAGER_APP_0_PROPERTY,
	LXSESSION_POWER_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_power_manager_app_properties[LXSESSION_POWER_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_FILE_MANAGER_APP (lxsession_file_manager_app_get_type ())
#define LXSESSION_FILE_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_FILE_MANAGER_APP, LxsessionFileManagerApp))
#define LXSESSION_FILE_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_FILE_MANAGER_APP, LxsessionFileManagerAppClass))
#define LXSESSION_IS_FILE_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_FILE_MANAGER_APP))
#define LXSESSION_IS_FILE_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_FILE_MANAGER_APP))
#define LXSESSION_FILE_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_FILE_MANAGER_APP, LxsessionFileManagerAppClass))

typedef struct _LxsessionFileManagerApp LxsessionFileManagerApp;
typedef struct _LxsessionFileManagerAppClass LxsessionFileManagerAppClass;
typedef struct _LxsessionFileManagerAppPrivate LxsessionFileManagerAppPrivate;
enum  {
	LXSESSION_FILE_MANAGER_APP_0_PROPERTY,
	LXSESSION_FILE_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_file_manager_app_properties[LXSESSION_FILE_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_DESKTOP_APP (lxsession_desktop_app_get_type ())
#define LXSESSION_DESKTOP_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_DESKTOP_APP, LxsessionDesktopApp))
#define LXSESSION_DESKTOP_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_DESKTOP_APP, LxsessionDesktopAppClass))
#define LXSESSION_IS_DESKTOP_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_DESKTOP_APP))
#define LXSESSION_IS_DESKTOP_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_DESKTOP_APP))
#define LXSESSION_DESKTOP_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_DESKTOP_APP, LxsessionDesktopAppClass))

typedef struct _LxsessionDesktopApp LxsessionDesktopApp;
typedef struct _LxsessionDesktopAppClass LxsessionDesktopAppClass;
typedef struct _LxsessionDesktopAppPrivate LxsessionDesktopAppPrivate;
enum  {
	LXSESSION_DESKTOP_APP_0_PROPERTY,
	LXSESSION_DESKTOP_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_desktop_app_properties[LXSESSION_DESKTOP_APP_NUM_PROPERTIES];
#define _g_object_unref0(var) ((var == NULL) ? NULL : (var = (g_object_unref (var), NULL)))

#define LXSESSION_TYPE_POLKIT_APP (lxsession_polkit_app_get_type ())
#define LXSESSION_POLKIT_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_POLKIT_APP, LxsessionPolkitApp))
#define LXSESSION_POLKIT_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_POLKIT_APP, LxsessionPolkitAppClass))
#define LXSESSION_IS_POLKIT_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_POLKIT_APP))
#define LXSESSION_IS_POLKIT_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_POLKIT_APP))
#define LXSESSION_POLKIT_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_POLKIT_APP, LxsessionPolkitAppClass))

typedef struct _LxsessionPolkitApp LxsessionPolkitApp;
typedef struct _LxsessionPolkitAppClass LxsessionPolkitAppClass;
typedef struct _LxsessionPolkitAppPrivate LxsessionPolkitAppPrivate;
enum  {
	LXSESSION_POLKIT_APP_0_PROPERTY,
	LXSESSION_POLKIT_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_polkit_app_properties[LXSESSION_POLKIT_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_NETWORK_GUI_APP (lxsession_network_gui_app_get_type ())
#define LXSESSION_NETWORK_GUI_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_NETWORK_GUI_APP, LxsessionNetworkGuiApp))
#define LXSESSION_NETWORK_GUI_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_NETWORK_GUI_APP, LxsessionNetworkGuiAppClass))
#define LXSESSION_IS_NETWORK_GUI_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_NETWORK_GUI_APP))
#define LXSESSION_IS_NETWORK_GUI_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_NETWORK_GUI_APP))
#define LXSESSION_NETWORK_GUI_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_NETWORK_GUI_APP, LxsessionNetworkGuiAppClass))

typedef struct _LxsessionNetworkGuiApp LxsessionNetworkGuiApp;
typedef struct _LxsessionNetworkGuiAppClass LxsessionNetworkGuiAppClass;
typedef struct _LxsessionNetworkGuiAppPrivate LxsessionNetworkGuiAppPrivate;
enum  {
	LXSESSION_NETWORK_GUI_APP_0_PROPERTY,
	LXSESSION_NETWORK_GUI_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_network_gui_app_properties[LXSESSION_NETWORK_GUI_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_AUDIO_MANAGER_APP (lxsession_audio_manager_app_get_type ())
#define LXSESSION_AUDIO_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_AUDIO_MANAGER_APP, LxsessionAudioManagerApp))
#define LXSESSION_AUDIO_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_AUDIO_MANAGER_APP, LxsessionAudioManagerAppClass))
#define LXSESSION_IS_AUDIO_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_AUDIO_MANAGER_APP))
#define LXSESSION_IS_AUDIO_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_AUDIO_MANAGER_APP))
#define LXSESSION_AUDIO_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_AUDIO_MANAGER_APP, LxsessionAudioManagerAppClass))

typedef struct _LxsessionAudioManagerApp LxsessionAudioManagerApp;
typedef struct _LxsessionAudioManagerAppClass LxsessionAudioManagerAppClass;
typedef struct _LxsessionAudioManagerAppPrivate LxsessionAudioManagerAppPrivate;
enum  {
	LXSESSION_AUDIO_MANAGER_APP_0_PROPERTY,
	LXSESSION_AUDIO_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_audio_manager_app_properties[LXSESSION_AUDIO_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_QUIT_MANAGER_APP (lxsession_quit_manager_app_get_type ())
#define LXSESSION_QUIT_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_QUIT_MANAGER_APP, LxsessionQuitManagerApp))
#define LXSESSION_QUIT_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_QUIT_MANAGER_APP, LxsessionQuitManagerAppClass))
#define LXSESSION_IS_QUIT_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_QUIT_MANAGER_APP))
#define LXSESSION_IS_QUIT_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_QUIT_MANAGER_APP))
#define LXSESSION_QUIT_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_QUIT_MANAGER_APP, LxsessionQuitManagerAppClass))

typedef struct _LxsessionQuitManagerApp LxsessionQuitManagerApp;
typedef struct _LxsessionQuitManagerAppClass LxsessionQuitManagerAppClass;
typedef struct _LxsessionQuitManagerAppPrivate LxsessionQuitManagerAppPrivate;
enum  {
	LXSESSION_QUIT_MANAGER_APP_0_PROPERTY,
	LXSESSION_QUIT_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_quit_manager_app_properties[LXSESSION_QUIT_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_WORKSPACE_MANAGER_APP (lxsession_workspace_manager_app_get_type ())
#define LXSESSION_WORKSPACE_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_WORKSPACE_MANAGER_APP, LxsessionWorkspaceManagerApp))
#define LXSESSION_WORKSPACE_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_WORKSPACE_MANAGER_APP, LxsessionWorkspaceManagerAppClass))
#define LXSESSION_IS_WORKSPACE_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_WORKSPACE_MANAGER_APP))
#define LXSESSION_IS_WORKSPACE_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_WORKSPACE_MANAGER_APP))
#define LXSESSION_WORKSPACE_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_WORKSPACE_MANAGER_APP, LxsessionWorkspaceManagerAppClass))

typedef struct _LxsessionWorkspaceManagerApp LxsessionWorkspaceManagerApp;
typedef struct _LxsessionWorkspaceManagerAppClass LxsessionWorkspaceManagerAppClass;
typedef struct _LxsessionWorkspaceManagerAppPrivate LxsessionWorkspaceManagerAppPrivate;
enum  {
	LXSESSION_WORKSPACE_MANAGER_APP_0_PROPERTY,
	LXSESSION_WORKSPACE_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_workspace_manager_app_properties[LXSESSION_WORKSPACE_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_LAUNCHER_MANAGER_APP (lxsession_launcher_manager_app_get_type ())
#define LXSESSION_LAUNCHER_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_LAUNCHER_MANAGER_APP, LxsessionLauncherManagerApp))
#define LXSESSION_LAUNCHER_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_LAUNCHER_MANAGER_APP, LxsessionLauncherManagerAppClass))
#define LXSESSION_IS_LAUNCHER_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_LAUNCHER_MANAGER_APP))
#define LXSESSION_IS_LAUNCHER_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_LAUNCHER_MANAGER_APP))
#define LXSESSION_LAUNCHER_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_LAUNCHER_MANAGER_APP, LxsessionLauncherManagerAppClass))

typedef struct _LxsessionLauncherManagerApp LxsessionLauncherManagerApp;
typedef struct _LxsessionLauncherManagerAppClass LxsessionLauncherManagerAppClass;
typedef struct _LxsessionLauncherManagerAppPrivate LxsessionLauncherManagerAppPrivate;
enum  {
	LXSESSION_LAUNCHER_MANAGER_APP_0_PROPERTY,
	LXSESSION_LAUNCHER_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_launcher_manager_app_properties[LXSESSION_LAUNCHER_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_TERMINAL_MANAGER_APP (lxsession_terminal_manager_app_get_type ())
#define LXSESSION_TERMINAL_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_TERMINAL_MANAGER_APP, LxsessionTerminalManagerApp))
#define LXSESSION_TERMINAL_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_TERMINAL_MANAGER_APP, LxsessionTerminalManagerAppClass))
#define LXSESSION_IS_TERMINAL_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_TERMINAL_MANAGER_APP))
#define LXSESSION_IS_TERMINAL_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_TERMINAL_MANAGER_APP))
#define LXSESSION_TERMINAL_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_TERMINAL_MANAGER_APP, LxsessionTerminalManagerAppClass))

typedef struct _LxsessionTerminalManagerApp LxsessionTerminalManagerApp;
typedef struct _LxsessionTerminalManagerAppClass LxsessionTerminalManagerAppClass;
typedef struct _LxsessionTerminalManagerAppPrivate LxsessionTerminalManagerAppPrivate;
enum  {
	LXSESSION_TERMINAL_MANAGER_APP_0_PROPERTY,
	LXSESSION_TERMINAL_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_terminal_manager_app_properties[LXSESSION_TERMINAL_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_PROXY_MANAGER_APP (lxsession_proxy_manager_app_get_type ())
#define LXSESSION_PROXY_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_PROXY_MANAGER_APP, LxsessionProxyManagerApp))
#define LXSESSION_PROXY_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_PROXY_MANAGER_APP, LxsessionProxyManagerAppClass))
#define LXSESSION_IS_PROXY_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_PROXY_MANAGER_APP))
#define LXSESSION_IS_PROXY_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_PROXY_MANAGER_APP))
#define LXSESSION_PROXY_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_PROXY_MANAGER_APP, LxsessionProxyManagerAppClass))

typedef struct _LxsessionProxyManagerApp LxsessionProxyManagerApp;
typedef struct _LxsessionProxyManagerAppClass LxsessionProxyManagerAppClass;
typedef struct _LxsessionProxyManagerAppPrivate LxsessionProxyManagerAppPrivate;
enum  {
	LXSESSION_PROXY_MANAGER_APP_0_PROPERTY,
	LXSESSION_PROXY_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_proxy_manager_app_properties[LXSESSION_PROXY_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_A11Y_APP (lxsession_a11y_app_get_type ())
#define LXSESSION_A11Y_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_A11Y_APP, LxsessionA11yApp))
#define LXSESSION_A11Y_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_A11Y_APP, LxsessionA11yAppClass))
#define LXSESSION_IS_A11Y_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_A11Y_APP))
#define LXSESSION_IS_A11Y_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_A11Y_APP))
#define LXSESSION_A11Y_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_A11Y_APP, LxsessionA11yAppClass))

typedef struct _LxsessionA11yApp LxsessionA11yApp;
typedef struct _LxsessionA11yAppClass LxsessionA11yAppClass;
typedef struct _LxsessionA11yAppPrivate LxsessionA11yAppPrivate;
enum  {
	LXSESSION_A11Y_APP_0_PROPERTY,
	LXSESSION_A11Y_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_a11y_app_properties[LXSESSION_A11Y_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_XRANDR_APP (lxsession_xrandr_app_get_type ())
#define LXSESSION_XRANDR_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_XRANDR_APP, LxsessionXrandrApp))
#define LXSESSION_XRANDR_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_XRANDR_APP, LxsessionXrandrAppClass))
#define LXSESSION_IS_XRANDR_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_XRANDR_APP))
#define LXSESSION_IS_XRANDR_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_XRANDR_APP))
#define LXSESSION_XRANDR_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_XRANDR_APP, LxsessionXrandrAppClass))

typedef struct _LxsessionXrandrApp LxsessionXrandrApp;
typedef struct _LxsessionXrandrAppClass LxsessionXrandrAppClass;
typedef struct _LxsessionXrandrAppPrivate LxsessionXrandrAppPrivate;
enum  {
	LXSESSION_XRANDR_APP_0_PROPERTY,
	LXSESSION_XRANDR_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_xrandr_app_properties[LXSESSION_XRANDR_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_KEYRING_APP (lxsession_keyring_app_get_type ())
#define LXSESSION_KEYRING_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_KEYRING_APP, LxsessionKeyringApp))
#define LXSESSION_KEYRING_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_KEYRING_APP, LxsessionKeyringAppClass))
#define LXSESSION_IS_KEYRING_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_KEYRING_APP))
#define LXSESSION_IS_KEYRING_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_KEYRING_APP))
#define LXSESSION_KEYRING_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_KEYRING_APP, LxsessionKeyringAppClass))

typedef struct _LxsessionKeyringApp LxsessionKeyringApp;
typedef struct _LxsessionKeyringAppClass LxsessionKeyringAppClass;
typedef struct _LxsessionKeyringAppPrivate LxsessionKeyringAppPrivate;
enum  {
	LXSESSION_KEYRING_APP_0_PROPERTY,
	LXSESSION_KEYRING_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_keyring_app_properties[LXSESSION_KEYRING_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_SCREENSHOT_MANAGER_APP (lxsession_screenshot_manager_app_get_type ())
#define LXSESSION_SCREENSHOT_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_SCREENSHOT_MANAGER_APP, LxsessionScreenshotManagerApp))
#define LXSESSION_SCREENSHOT_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_SCREENSHOT_MANAGER_APP, LxsessionScreenshotManagerAppClass))
#define LXSESSION_IS_SCREENSHOT_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_SCREENSHOT_MANAGER_APP))
#define LXSESSION_IS_SCREENSHOT_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_SCREENSHOT_MANAGER_APP))
#define LXSESSION_SCREENSHOT_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_SCREENSHOT_MANAGER_APP, LxsessionScreenshotManagerAppClass))

typedef struct _LxsessionScreenshotManagerApp LxsessionScreenshotManagerApp;
typedef struct _LxsessionScreenshotManagerAppClass LxsessionScreenshotManagerAppClass;
typedef struct _LxsessionScreenshotManagerAppPrivate LxsessionScreenshotManagerAppPrivate;
enum  {
	LXSESSION_SCREENSHOT_MANAGER_APP_0_PROPERTY,
	LXSESSION_SCREENSHOT_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_screenshot_manager_app_properties[LXSESSION_SCREENSHOT_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_UPDATES_MANAGER_APP (lxsession_updates_manager_app_get_type ())
#define LXSESSION_UPDATES_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_UPDATES_MANAGER_APP, LxsessionUpdatesManagerApp))
#define LXSESSION_UPDATES_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_UPDATES_MANAGER_APP, LxsessionUpdatesManagerAppClass))
#define LXSESSION_IS_UPDATES_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_UPDATES_MANAGER_APP))
#define LXSESSION_IS_UPDATES_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_UPDATES_MANAGER_APP))
#define LXSESSION_UPDATES_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_UPDATES_MANAGER_APP, LxsessionUpdatesManagerAppClass))

typedef struct _LxsessionUpdatesManagerApp LxsessionUpdatesManagerApp;
typedef struct _LxsessionUpdatesManagerAppClass LxsessionUpdatesManagerAppClass;
typedef struct _LxsessionUpdatesManagerAppPrivate LxsessionUpdatesManagerAppPrivate;

#define LXSESSION_TYPE_ICON_OBJECT (lxsession_icon_object_get_type ())
#define LXSESSION_ICON_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_ICON_OBJECT, LxsessionIconObject))
#define LXSESSION_ICON_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_ICON_OBJECT, LxsessionIconObjectClass))
#define LXSESSION_IS_ICON_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_ICON_OBJECT))
#define LXSESSION_IS_ICON_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_ICON_OBJECT))
#define LXSESSION_ICON_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_ICON_OBJECT, LxsessionIconObjectClass))

typedef struct _LxsessionIconObject LxsessionIconObject;
typedef struct _LxsessionIconObjectClass LxsessionIconObjectClass;

#define LXSESSION_TYPE_MENU_OBJECT (lxsession_menu_object_get_type ())
#define LXSESSION_MENU_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_MENU_OBJECT, LxsessionMenuObject))
#define LXSESSION_MENU_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_MENU_OBJECT, LxsessionMenuObjectClass))
#define LXSESSION_IS_MENU_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_MENU_OBJECT))
#define LXSESSION_IS_MENU_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_MENU_OBJECT))
#define LXSESSION_MENU_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_MENU_OBJECT, LxsessionMenuObjectClass))

typedef struct _LxsessionMenuObject LxsessionMenuObject;
typedef struct _LxsessionMenuObjectClass LxsessionMenuObjectClass;
enum  {
	LXSESSION_UPDATES_MANAGER_APP_0_PROPERTY,
	LXSESSION_UPDATES_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_updates_manager_app_properties[LXSESSION_UPDATES_MANAGER_APP_NUM_PROPERTIES];

#define LXSESSION_TYPE_SESSION_OBJECT (lxsession_session_object_get_type ())
#define LXSESSION_SESSION_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_SESSION_OBJECT, LxsessionSessionObject))
#define LXSESSION_SESSION_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_SESSION_OBJECT, LxsessionSessionObjectClass))
#define LXSESSION_IS_SESSION_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_SESSION_OBJECT))
#define LXSESSION_IS_SESSION_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_SESSION_OBJECT))
#define LXSESSION_SESSION_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_SESSION_OBJECT, LxsessionSessionObjectClass))

typedef struct _LxsessionSessionObject LxsessionSessionObject;
typedef struct _LxsessionSessionObjectClass LxsessionSessionObjectClass;

#define LXSESSION_TYPE_MENU_ITEM_OBJECT (lxsession_menu_item_object_get_type ())
#define LXSESSION_MENU_ITEM_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_MENU_ITEM_OBJECT, LxsessionMenuItemObject))
#define LXSESSION_MENU_ITEM_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_MENU_ITEM_OBJECT, LxsessionMenuItemObjectClass))
#define LXSESSION_IS_MENU_ITEM_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_MENU_ITEM_OBJECT))
#define LXSESSION_IS_MENU_ITEM_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_MENU_ITEM_OBJECT))
#define LXSESSION_MENU_ITEM_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_MENU_ITEM_OBJECT, LxsessionMenuItemObjectClass))

typedef struct _LxsessionMenuItemObject LxsessionMenuItemObject;
typedef struct _LxsessionMenuItemObjectClass LxsessionMenuItemObjectClass;
typedef void (*LxsessionIconObjectActionCallback) (gpointer user_data);
typedef struct _Block1Data Block1Data;

#define LXSESSION_TYPE_CRASH_MANAGER_APP (lxsession_crash_manager_app_get_type ())
#define LXSESSION_CRASH_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LXSESSION_TYPE_CRASH_MANAGER_APP, LxsessionCrashManagerApp))
#define LXSESSION_CRASH_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LXSESSION_TYPE_CRASH_MANAGER_APP, LxsessionCrashManagerAppClass))
#define LXSESSION_IS_CRASH_MANAGER_APP(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LXSESSION_TYPE_CRASH_MANAGER_APP))
#define LXSESSION_IS_CRASH_MANAGER_APP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), LXSESSION_TYPE_CRASH_MANAGER_APP))
#define LXSESSION_CRASH_MANAGER_APP_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), LXSESSION_TYPE_CRASH_MANAGER_APP, LxsessionCrashManagerAppClass))

typedef struct _LxsessionCrashManagerApp LxsessionCrashManagerApp;
typedef struct _LxsessionCrashManagerAppClass LxsessionCrashManagerAppClass;
typedef struct _LxsessionCrashManagerAppPrivate LxsessionCrashManagerAppPrivate;
enum  {
	LXSESSION_CRASH_MANAGER_APP_0_PROPERTY,
	LXSESSION_CRASH_MANAGER_APP_NUM_PROPERTIES
};
static GParamSpec* lxsession_crash_manager_app_properties[LXSESSION_CRASH_MANAGER_APP_NUM_PROPERTIES];
#define _g_regex_unref0(var) ((var == NULL) ? NULL : (var = (g_regex_unref (var), NULL)))
typedef struct _Block2Data Block2Data;
typedef struct _Block3Data Block3Data;

struct _LxsessionAppObject {
	GObject parent_instance;
	LxsessionAppObjectPrivate * priv;
	GPid pid;
	gboolean intentional_stop;
	gboolean reload_pending;
	guint restart_source_id;
	gint64 launch_time_us;
};

struct _LxsessionAppObjectClass {
	GObjectClass parent_class;
	void (*read_config_settings) (LxsessionAppObject* self);
	void (*read_settings) (LxsessionAppObject* self);
};

struct _LxsessionAppObjectPrivate {
	gchar* _name;
	gchar** _command;
	gint _command_length1;
	gint __command_size_;
	gboolean _guard;
	gchar* _application_type;
	gint _crash_count;
	gint _stop_reload;
};

struct _LxsessionSimpleAppObject {
	LxsessionAppObject parent_instance;
	LxsessionSimpleAppObjectPrivate * priv;
};

struct _LxsessionSimpleAppObjectClass {
	LxsessionAppObjectClass parent_class;
};

struct _LxsessionGenericAppObject {
	LxsessionAppObject parent_instance;
	LxsessionGenericAppObjectPrivate * priv;
};

struct _LxsessionGenericAppObjectClass {
	LxsessionAppObjectClass parent_class;
};

struct _LxsessionAppType {
	gchar* name;
	gchar** command;
	gint command_length1;
	gboolean guard;
	gchar* application_type;
};

struct _LxsessionGenericSimpleApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionGenericSimpleAppPrivate * priv;
};

struct _LxsessionGenericSimpleAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionGenericSimpleAppPrivate {
	gchar* _settings_command;
};

struct _LxsessionWindowsManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionWindowsManagerAppPrivate * priv;
};

struct _LxsessionWindowsManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionWindowsManagerAppPrivate {
	gchar* wm_command;
	gchar* mode;
	gchar* session;
	gchar* extras;
};

struct _LxsessionPanelApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionPanelAppPrivate * priv;
	gchar* panel_command;
	gchar* panel_session;
};

struct _LxsessionPanelAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionDockApp {
	LxsessionPanelApp parent_instance;
	LxsessionDockAppPrivate * priv;
};

struct _LxsessionDockAppClass {
	LxsessionPanelAppClass parent_class;
};

struct _LxsessionScreensaverApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionScreensaverAppPrivate * priv;
};

struct _LxsessionScreensaverAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionScreensaverAppPrivate {
	gchar* screensaver_command;
};

struct _LxsessionPowerManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionPowerManagerAppPrivate * priv;
};

struct _LxsessionPowerManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionPowerManagerAppPrivate {
	gchar* powermanager_command;
	gchar* laptop_mode;
};

struct _LxsessionFileManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionFileManagerAppPrivate * priv;
};

struct _LxsessionFileManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionFileManagerAppPrivate {
	gchar* filemanager_command;
	gchar* filemanager_session;
	gchar* filemanager_extras;
};

struct _LxsessionDesktopApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionDesktopAppPrivate * priv;
};

struct _LxsessionDesktopAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionDesktopAppPrivate {
	gchar* desktop_command;
	gchar* desktop_wallpaper;
};

struct _LxsessionPolkitApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionPolkitAppPrivate * priv;
};

struct _LxsessionPolkitAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionPolkitAppPrivate {
	gchar* polkit_command;
};

struct _LxsessionNetworkGuiApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionNetworkGuiAppPrivate * priv;
};

struct _LxsessionNetworkGuiAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionNetworkGuiAppPrivate {
	gchar* network_command;
	gchar* laptop_mode;
};

struct _LxsessionAudioManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionAudioManagerAppPrivate * priv;
};

struct _LxsessionAudioManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionAudioManagerAppPrivate {
	gchar* audiomanager_command;
};

struct _LxsessionQuitManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionQuitManagerAppPrivate * priv;
};

struct _LxsessionQuitManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionQuitManagerAppPrivate {
	gchar* quitmanager_command;
	gchar* quitmanager_image;
	gchar* quitmanager_layout;
};

struct _LxsessionWorkspaceManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionWorkspaceManagerAppPrivate * priv;
};

struct _LxsessionWorkspaceManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionWorkspaceManagerAppPrivate {
	gchar* workspacemanager_command;
};

struct _LxsessionLauncherManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionLauncherManagerAppPrivate * priv;
};

struct _LxsessionLauncherManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionLauncherManagerAppPrivate {
	gchar* launchermanager_command;
};

struct _LxsessionTerminalManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionTerminalManagerAppPrivate * priv;
};

struct _LxsessionTerminalManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionTerminalManagerAppPrivate {
	gchar* terminalmanager_command;
};

struct _LxsessionProxyManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionProxyManagerAppPrivate * priv;
};

struct _LxsessionProxyManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionProxyManagerAppPrivate {
	gchar* proxymanager_command;
	gchar* proxymanager_http;
};

struct _LxsessionA11yApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionA11yAppPrivate * priv;
};

struct _LxsessionA11yAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionA11yAppPrivate {
	gchar* a11y_command;
};

struct _LxsessionXrandrApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionXrandrAppPrivate * priv;
};

struct _LxsessionXrandrAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionXrandrAppPrivate {
	gchar* xrandr_command;
};

struct _LxsessionKeyringApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionKeyringAppPrivate * priv;
};

struct _LxsessionKeyringAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionKeyringAppPrivate {
	gchar* keyring_command;
	gchar* keyring_type;
};

struct _LxsessionScreenshotManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionScreenshotManagerAppPrivate * priv;
};

struct _LxsessionScreenshotManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionScreenshotManagerAppPrivate {
	gchar* screenshotmanager_command;
};

struct _LxsessionUpdatesManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionUpdatesManagerAppPrivate * priv;
};

struct _LxsessionUpdatesManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionUpdatesManagerAppPrivate {
	gchar* updatesmanager_command;
	LxsessionIconObject* updates_icon;
	LxsessionMenuObject* icon_menu;
	LxsessionIconObject* language_icon;
	LxsessionIconObject* reboot_icon;
	gchar* apt_update_path;
	GFile* apt_update_file;
	GFileMonitor* apt_update_monitor;
	gchar* dpkg_update_path;
	GFile* dpkg_update_file;
	GFileMonitor* dpkg_update_monitor;
	gchar* dpkg_run_path;
	GFile* dpkg_run_file;
	GFileMonitor* dpkg_run_monitor;
	gchar* apt_lists_update_path;
	GFile* apt_lists_update_file;
	GFileMonitor* apt_lists_update_monitor;
	gchar* reboot_path;
	GFile* reboot_file;
	GFileMonitor* reboot_monitor;
	gchar* dpkg_lock_file;
	gint lock_check;
	gboolean apt_check_pending;
	gboolean apt_check_result_ready;
	gchar* apt_check_stdout;
	gchar* apt_check_stderr;
	gint apt_check_exit_status;
};

struct _Block1Data {
	int _ref_count_;
	LxsessionUpdatesManagerApp* self;
	gchar* upgrade_manager_command;
};

struct _LxsessionCrashManagerApp {
	LxsessionSimpleAppObject parent_instance;
	LxsessionCrashManagerAppPrivate * priv;
};

struct _LxsessionCrashManagerAppClass {
	LxsessionSimpleAppObjectClass parent_class;
};

struct _LxsessionCrashManagerAppPrivate {
	gchar* crash_manager_command;
	LxsessionIconObject* crash_icon;
	gchar* crash_dir_path;
	GFile* crash_dir_file;
	GFileMonitor* crash_dir_monitor;
	gint lock_crash_check;
};

struct _Block2Data {
	int _ref_count_;
	LxsessionCrashManagerApp* self;
	gchar* command;
};

struct _Block3Data {
	int _ref_count_;
	LxsessionCrashManagerApp* self;
	gchar* remove_command;
};

static gint LxsessionAppObject_private_offset;
static gpointer lxsession_app_object_parent_class = NULL;
static gpointer lxsession_simple_app_object_parent_class = NULL;
static gpointer lxsession_generic_app_object_parent_class = NULL;
static gint LxsessionGenericSimpleApp_private_offset;
static gpointer lxsession_generic_simple_app_parent_class = NULL;
static gint LxsessionWindowsManagerApp_private_offset;
static gpointer lxsession_windows_manager_app_parent_class = NULL;
VALA_EXTERN LxsessionLxsessionConfigKeyFile* lxsession_global_settings;
static gpointer lxsession_panel_app_parent_class = NULL;
static gpointer lxsession_dock_app_parent_class = NULL;
static gint LxsessionScreensaverApp_private_offset;
static gpointer lxsession_screensaver_app_parent_class = NULL;
static gint LxsessionPowerManagerApp_private_offset;
static gpointer lxsession_power_manager_app_parent_class = NULL;
static gint LxsessionFileManagerApp_private_offset;
static gpointer lxsession_file_manager_app_parent_class = NULL;
static gint LxsessionDesktopApp_private_offset;
static gpointer lxsession_desktop_app_parent_class = NULL;
VALA_EXTERN LxsessionFileManagerApp* lxsession_global_file_manager;
static gint LxsessionPolkitApp_private_offset;
static gpointer lxsession_polkit_app_parent_class = NULL;
static gint LxsessionNetworkGuiApp_private_offset;
static gpointer lxsession_network_gui_app_parent_class = NULL;
static gint LxsessionAudioManagerApp_private_offset;
static gpointer lxsession_audio_manager_app_parent_class = NULL;
static gint LxsessionQuitManagerApp_private_offset;
static gpointer lxsession_quit_manager_app_parent_class = NULL;
static gint LxsessionWorkspaceManagerApp_private_offset;
static gpointer lxsession_workspace_manager_app_parent_class = NULL;
static gint LxsessionLauncherManagerApp_private_offset;
static gpointer lxsession_launcher_manager_app_parent_class = NULL;
static gint LxsessionTerminalManagerApp_private_offset;
static gpointer lxsession_terminal_manager_app_parent_class = NULL;
static gint LxsessionProxyManagerApp_private_offset;
static gpointer lxsession_proxy_manager_app_parent_class = NULL;
static gint LxsessionA11yApp_private_offset;
static gpointer lxsession_a11y_app_parent_class = NULL;
static gint LxsessionXrandrApp_private_offset;
static gpointer lxsession_xrandr_app_parent_class = NULL;
static gint LxsessionKeyringApp_private_offset;
static gpointer lxsession_keyring_app_parent_class = NULL;
static gint LxsessionScreenshotManagerApp_private_offset;
static gpointer lxsession_screenshot_manager_app_parent_class = NULL;
static gint LxsessionUpdatesManagerApp_private_offset;
static gpointer lxsession_updates_manager_app_parent_class = NULL;
static gint LxsessionCrashManagerApp_private_offset;
static gpointer lxsession_crash_manager_app_parent_class = NULL;

VALA_EXTERN GType lxsession_app_object_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionAppObject, g_object_unref)
VALA_EXTERN void lxsession_app_object_read_config_settings (LxsessionAppObject* self);
VALA_EXTERN void lxsession_app_object_read_settings (LxsessionAppObject* self);
VALA_EXTERN LxsessionAppObject* lxsession_app_object_new (void);
VALA_EXTERN LxsessionAppObject* lxsession_app_object_construct (GType object_type);
VALA_EXTERN void lxsession_app_object_init (LxsessionAppObject* self);
VALA_EXTERN void lxsession_app_object_launch (LxsessionAppObject* self);
VALA_EXTERN void lxsession_app_object_generic_launch (LxsessionAppObject* self,
                                          const gchar* arg1);
VALA_EXTERN const gchar* lxsession_app_object_get_name (LxsessionAppObject* self);
VALA_EXTERN gchar** lxsession_app_object_get_command (LxsessionAppObject* self,
                                          gint* result_length1);
static void lxsession_app_object_callback_pid (LxsessionAppObject* self,
                                        GPid pid,
                                        gint status);
static void _lxsession_app_object_callback_pid_gchild_watch_func (GPid pid,
                                                           gint wait_status,
                                                           gpointer self);
static void lxsession_app_object_real_read_config_settings (LxsessionAppObject* self);
static void lxsession_app_object_real_read_settings (LxsessionAppObject* self);
VALA_EXTERN void lxsession_app_object_stop (LxsessionAppObject* self);
VALA_EXTERN void lxsession_app_object_reload (LxsessionAppObject* self);
VALA_EXTERN gboolean lxsession_app_object_get_guard (LxsessionAppObject* self);
VALA_EXTERN gint lxsession_app_object_get_crash_count (LxsessionAppObject* self);
VALA_EXTERN void lxsession_app_object_set_crash_count (LxsessionAppObject* self,
                                           gint value);
VALA_EXTERN gint lxsession_app_object_get_stop_reload (LxsessionAppObject* self);
VALA_EXTERN void lxsession_app_object_set_name (LxsessionAppObject* self,
                                    const gchar* value);
VALA_EXTERN void lxsession_app_object_set_command (LxsessionAppObject* self,
                                       gchar** value,
                                       gint value_length1);
static gchar** _vala_array_dup1 (gchar** self,
                          gssize length);
VALA_EXTERN void lxsession_app_object_set_guard (LxsessionAppObject* self,
                                     gboolean value);
VALA_EXTERN const gchar* lxsession_app_object_get_application_type (LxsessionAppObject* self);
VALA_EXTERN void lxsession_app_object_set_application_type (LxsessionAppObject* self,
                                                const gchar* value);
VALA_EXTERN void lxsession_app_object_set_stop_reload (LxsessionAppObject* self,
                                           gint value);
static void lxsession_app_object_finalize (GObject * obj);
static GType lxsession_app_object_get_type_once (void);
static void _vala_lxsession_app_object_get_property (GObject * object,
                                              guint property_id,
                                              GValue * value,
                                              GParamSpec * pspec);
static void _vala_lxsession_app_object_set_property (GObject * object,
                                              guint property_id,
                                              const GValue * value,
                                              GParamSpec * pspec);
VALA_EXTERN GType lxsession_simple_app_object_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionSimpleAppObject, g_object_unref)
VALA_EXTERN LxsessionSimpleAppObject* lxsession_simple_app_object_new (void);
VALA_EXTERN LxsessionSimpleAppObject* lxsession_simple_app_object_construct (GType object_type);
static GType lxsession_simple_app_object_get_type_once (void);
VALA_EXTERN GType lxsession_generic_app_object_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionGenericAppObject, g_object_unref)
VALA_EXTERN GType lxsession_app_type_get_type (void) G_GNUC_CONST ;
VALA_EXTERN LxsessionAppType* lxsession_app_type_dup (const LxsessionAppType* self);
VALA_EXTERN void lxsession_app_type_free (LxsessionAppType* self);
VALA_EXTERN void lxsession_app_type_copy (const LxsessionAppType* self,
                              LxsessionAppType* dest);
VALA_EXTERN void lxsession_app_type_destroy (LxsessionAppType* self);
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (LxsessionAppType, lxsession_app_type_destroy)
VALA_EXTERN LxsessionGenericAppObject* lxsession_generic_app_object_new (LxsessionAppType* app_type);
VALA_EXTERN LxsessionGenericAppObject* lxsession_generic_app_object_construct (GType object_type,
                                                                   LxsessionAppType* app_type);
static GType lxsession_generic_app_object_get_type_once (void);
VALA_EXTERN GType lxsession_generic_simple_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionGenericSimpleApp, g_object_unref)
VALA_EXTERN LxsessionGenericSimpleApp* lxsession_generic_simple_app_new (const gchar* argument);
VALA_EXTERN LxsessionGenericSimpleApp* lxsession_generic_simple_app_construct (GType object_type,
                                                                   const gchar* argument);
VALA_EXTERN void lxsession_generic_simple_app_set_settings_command (LxsessionGenericSimpleApp* self,
                                                        const gchar* value);
static void lxsession_generic_simple_app_real_read_settings (LxsessionAppObject* base);
VALA_EXTERN const gchar* lxsession_generic_simple_app_get_settings_command (LxsessionGenericSimpleApp* self);
static void lxsession_generic_simple_app_finalize (GObject * obj);
static GType lxsession_generic_simple_app_get_type_once (void);
static void _vala_lxsession_generic_simple_app_get_property (GObject * object,
                                                      guint property_id,
                                                      GValue * value,
                                                      GParamSpec * pspec);
static void _vala_lxsession_generic_simple_app_set_property (GObject * object,
                                                      guint property_id,
                                                      const GValue * value,
                                                      GParamSpec * pspec);
VALA_EXTERN GType lxsession_windows_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionWindowsManagerApp, g_object_unref)
VALA_EXTERN LxsessionWindowsManagerApp* lxsession_windows_manager_app_new (void);
VALA_EXTERN LxsessionWindowsManagerApp* lxsession_windows_manager_app_construct (GType object_type);
static void lxsession_windows_manager_app_real_read_settings (LxsessionAppObject* base);
VALA_EXTERN GType lxsession_lxsession_config_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionLxsessionConfig, g_object_unref)
VALA_EXTERN GType lxsession_lxsession_config_key_file_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionLxsessionConfigKeyFile, g_object_unref)
VALA_EXTERN gchar* lxsession_lxsession_config_get_item_string (LxsessionLxsessionConfig* self,
                                                   const gchar* categorie,
                                                   const gchar* key1,
                                                   const gchar* key2);
static gchar* lxsession_windows_manager_app_find_window_manager (LxsessionWindowsManagerApp* self);
static void lxsession_windows_manager_app_fallback_window_manager (LxsessionWindowsManagerApp* self);
static void _g_free0_ (gpointer var);
static void _vala_gchar_free_function_content_of (gpointer data);
static void lxsession_windows_manager_app_callback_pid (LxsessionWindowsManagerApp* self,
                                                 GPid pid,
                                                 gint status);
VALA_EXTERN void lxsession_lxsession_config_set_generic_default (LxsessionLxsessionConfig* self,
                                                     const gchar* categorie,
                                                     const gchar* key1,
                                                     const gchar* key2,
                                                     const gchar* type,
                                                     const gchar* default_value);
VALA_EXTERN void lxsession_windows_manager_app_launch (LxsessionWindowsManagerApp* self);
static void _lxsession_windows_manager_app_callback_pid_gchild_watch_func (GPid pid,
                                                                    gint wait_status,
                                                                    gpointer self);
static void lxsession_windows_manager_app_finalize (GObject * obj);
static GType lxsession_windows_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_panel_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionPanelApp, g_object_unref)
VALA_EXTERN LxsessionPanelApp* lxsession_panel_app_new (void);
VALA_EXTERN LxsessionPanelApp* lxsession_panel_app_construct (GType object_type);
static void lxsession_panel_app_real_read_config_settings (LxsessionAppObject* base);
static void lxsession_panel_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_panel_app_finalize (GObject * obj);
static GType lxsession_panel_app_get_type_once (void);
VALA_EXTERN GType lxsession_dock_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionDockApp, g_object_unref)
VALA_EXTERN LxsessionDockApp* lxsession_dock_app_new (void);
VALA_EXTERN LxsessionDockApp* lxsession_dock_app_construct (GType object_type);
static void lxsession_dock_app_real_read_config_settings (LxsessionAppObject* base);
static GType lxsession_dock_app_get_type_once (void);
VALA_EXTERN GType lxsession_screensaver_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionScreensaverApp, g_object_unref)
VALA_EXTERN LxsessionScreensaverApp* lxsession_screensaver_app_new (void);
VALA_EXTERN LxsessionScreensaverApp* lxsession_screensaver_app_construct (GType object_type);
static void lxsession_screensaver_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_screensaver_app_finalize (GObject * obj);
static GType lxsession_screensaver_app_get_type_once (void);
VALA_EXTERN GType lxsession_power_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionPowerManagerApp, g_object_unref)
VALA_EXTERN LxsessionPowerManagerApp* lxsession_power_manager_app_new (void);
VALA_EXTERN LxsessionPowerManagerApp* lxsession_power_manager_app_construct (GType object_type);
static void lxsession_power_manager_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_power_manager_app_finalize (GObject * obj);
static GType lxsession_power_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_file_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionFileManagerApp, g_object_unref)
VALA_EXTERN LxsessionFileManagerApp* lxsession_file_manager_app_new (void);
VALA_EXTERN LxsessionFileManagerApp* lxsession_file_manager_app_construct (GType object_type);
static void lxsession_file_manager_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_file_manager_app_finalize (GObject * obj);
static GType lxsession_file_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_desktop_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionDesktopApp, g_object_unref)
VALA_EXTERN LxsessionDesktopApp* lxsession_desktop_app_new (void);
VALA_EXTERN LxsessionDesktopApp* lxsession_desktop_app_construct (GType object_type);
static void lxsession_desktop_app_real_read_settings (LxsessionAppObject* base);
VALA_EXTERN void lxsession_desktop_app_launch_settings (LxsessionDesktopApp* self);
static gchar** _vala_array_dup2 (gchar** self,
                          gssize length);
static void lxsession_desktop_app_finalize (GObject * obj);
static GType lxsession_desktop_app_get_type_once (void);
VALA_EXTERN GType lxsession_polkit_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionPolkitApp, g_object_unref)
VALA_EXTERN LxsessionPolkitApp* lxsession_polkit_app_new (void);
VALA_EXTERN LxsessionPolkitApp* lxsession_polkit_app_construct (GType object_type);
static void lxsession_polkit_app_real_read_settings (LxsessionAppObject* base);
VALA_EXTERN void lxsession_polkit_app_deactivate (LxsessionPolkitApp* self);
static void lxsession_polkit_app_finalize (GObject * obj);
static GType lxsession_polkit_app_get_type_once (void);
VALA_EXTERN GType lxsession_network_gui_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionNetworkGuiApp, g_object_unref)
VALA_EXTERN LxsessionNetworkGuiApp* lxsession_network_gui_app_new (void);
VALA_EXTERN LxsessionNetworkGuiApp* lxsession_network_gui_app_construct (GType object_type);
static void lxsession_network_gui_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_network_gui_app_finalize (GObject * obj);
static GType lxsession_network_gui_app_get_type_once (void);
VALA_EXTERN GType lxsession_audio_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionAudioManagerApp, g_object_unref)
VALA_EXTERN LxsessionAudioManagerApp* lxsession_audio_manager_app_new (void);
VALA_EXTERN LxsessionAudioManagerApp* lxsession_audio_manager_app_construct (GType object_type);
static void lxsession_audio_manager_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_audio_manager_app_finalize (GObject * obj);
static GType lxsession_audio_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_quit_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionQuitManagerApp, g_object_unref)
VALA_EXTERN LxsessionQuitManagerApp* lxsession_quit_manager_app_new (void);
VALA_EXTERN LxsessionQuitManagerApp* lxsession_quit_manager_app_construct (GType object_type);
static void lxsession_quit_manager_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_quit_manager_app_finalize (GObject * obj);
static GType lxsession_quit_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_workspace_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionWorkspaceManagerApp, g_object_unref)
VALA_EXTERN LxsessionWorkspaceManagerApp* lxsession_workspace_manager_app_new (void);
VALA_EXTERN LxsessionWorkspaceManagerApp* lxsession_workspace_manager_app_construct (GType object_type);
static void lxsession_workspace_manager_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_workspace_manager_app_finalize (GObject * obj);
static GType lxsession_workspace_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_launcher_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionLauncherManagerApp, g_object_unref)
VALA_EXTERN LxsessionLauncherManagerApp* lxsession_launcher_manager_app_new (void);
VALA_EXTERN LxsessionLauncherManagerApp* lxsession_launcher_manager_app_construct (GType object_type);
static void lxsession_launcher_manager_app_real_read_settings (LxsessionAppObject* base);
VALA_EXTERN void lxsession_launcher_manager_app_autostart_launch (LxsessionLauncherManagerApp* self);
VALA_EXTERN void lxsession_luxesession_spawn_command_line_async (const gchar* command_line);
static void lxsession_launcher_manager_app_finalize (GObject * obj);
static GType lxsession_launcher_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_terminal_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionTerminalManagerApp, g_object_unref)
VALA_EXTERN LxsessionTerminalManagerApp* lxsession_terminal_manager_app_new (void);
VALA_EXTERN LxsessionTerminalManagerApp* lxsession_terminal_manager_app_construct (GType object_type);
static void lxsession_terminal_manager_app_real_read_config_settings (LxsessionAppObject* base);
static void lxsession_terminal_manager_app_real_read_settings (LxsessionAppObject* base);
VALA_EXTERN void lxsession_terminal_manager_app_launch (LxsessionTerminalManagerApp* self,
                                            const gchar* argument);
static void lxsession_terminal_manager_app_finalize (GObject * obj);
static GType lxsession_terminal_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_proxy_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionProxyManagerApp, g_object_unref)
VALA_EXTERN LxsessionProxyManagerApp* lxsession_proxy_manager_app_new (void);
VALA_EXTERN LxsessionProxyManagerApp* lxsession_proxy_manager_app_construct (GType object_type);
static void lxsession_proxy_manager_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_proxy_manager_app_finalize (GObject * obj);
static GType lxsession_proxy_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_a11y_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionA11yApp, g_object_unref)
VALA_EXTERN LxsessionA11yApp* lxsession_a11y_app_new (void);
VALA_EXTERN LxsessionA11yApp* lxsession_a11y_app_construct (GType object_type);
static void lxsession_a11y_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_a11y_app_finalize (GObject * obj);
static GType lxsession_a11y_app_get_type_once (void);
VALA_EXTERN GType lxsession_xrandr_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionXrandrApp, g_object_unref)
VALA_EXTERN LxsessionXrandrApp* lxsession_xrandr_app_new (void);
VALA_EXTERN LxsessionXrandrApp* lxsession_xrandr_app_construct (GType object_type);
static void lxsession_xrandr_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_xrandr_app_finalize (GObject * obj);
static GType lxsession_xrandr_app_get_type_once (void);
VALA_EXTERN GType lxsession_keyring_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionKeyringApp, g_object_unref)
VALA_EXTERN LxsessionKeyringApp* lxsession_keyring_app_new (void);
VALA_EXTERN LxsessionKeyringApp* lxsession_keyring_app_construct (GType object_type);
static void lxsession_keyring_app_real_read_settings (LxsessionAppObject* base);
static void lxsession_keyring_app_finalize (GObject * obj);
static GType lxsession_keyring_app_get_type_once (void);
VALA_EXTERN GType lxsession_screenshot_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionScreenshotManagerApp, g_object_unref)
VALA_EXTERN LxsessionScreenshotManagerApp* lxsession_screenshot_manager_app_new (void);
VALA_EXTERN LxsessionScreenshotManagerApp* lxsession_screenshot_manager_app_construct (GType object_type);
static void lxsession_screenshot_manager_app_real_read_settings (LxsessionAppObject* base);
VALA_EXTERN void lxsession_screenshot_manager_app_window_launch (LxsessionScreenshotManagerApp* self);
static gchar** _vala_array_dup3 (gchar** self,
                          gssize length);
static void lxsession_screenshot_manager_app_finalize (GObject * obj);
static GType lxsession_screenshot_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_updates_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionUpdatesManagerApp, g_object_unref)
VALA_EXTERN GType lxsession_icon_object_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionIconObject, g_object_unref)
VALA_EXTERN GType lxsession_menu_object_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionMenuObject, g_object_unref)
VALA_EXTERN LxsessionUpdatesManagerApp* lxsession_updates_manager_app_new (void);
VALA_EXTERN LxsessionUpdatesManagerApp* lxsession_updates_manager_app_construct (GType object_type);
static void lxsession_updates_manager_app_real_read_settings (LxsessionAppObject* base);
VALA_EXTERN void lxsession_updates_manager_app_setup_apt_config (LxsessionUpdatesManagerApp* self);
VALA_EXTERN void lxsession_updates_manager_app_setup_reboot_config (LxsessionUpdatesManagerApp* self);
VALA_EXTERN void lxsession_updates_manager_app_run_check (LxsessionUpdatesManagerApp* self);
VALA_EXTERN void lxsession_updates_manager_app_test_inactivate (LxsessionUpdatesManagerApp* self);
VALA_EXTERN void lxsession_icon_object_inactivate (LxsessionIconObject* self);
VALA_EXTERN void lxsession_updates_manager_app_test_activate (LxsessionUpdatesManagerApp* self);
VALA_EXTERN void lxsession_icon_object_activate (LxsessionIconObject* self);
VALA_EXTERN gboolean lxsession_updates_manager_app_check_lock_file (LxsessionUpdatesManagerApp* self,
                                                        const gchar* check_file);
VALA_EXTERN gboolean lxsession_updates_manager_app_on_apt_update_file_change (LxsessionUpdatesManagerApp* self);
static gboolean _lxsession_updates_manager_app_on_apt_update_file_change_gsource_func (gpointer self);
VALA_EXTERN void lxsession_updates_manager_app_reboot_launch (LxsessionUpdatesManagerApp* self);
VALA_EXTERN GType lxsession_session_object_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionSessionObject, g_object_unref)
VALA_EXTERN LxsessionSessionObject* lxsession_session_object_new (void);
VALA_EXTERN LxsessionSessionObject* lxsession_session_object_construct (GType object_type);
VALA_EXTERN void lxsession_session_object_luxesession_restart (LxsessionSessionObject* self);
VALA_EXTERN void lxsession_updates_manager_app_run_check_reboot (LxsessionUpdatesManagerApp* self);
VALA_EXTERN LxsessionMenuObject* lxsession_menu_object_new (void);
VALA_EXTERN LxsessionMenuObject* lxsession_menu_object_construct (GType object_type);
VALA_EXTERN GType lxsession_menu_item_object_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionMenuItemObject, g_object_unref)
VALA_EXTERN LxsessionMenuItemObject* lxsession_menu_item_object_new (void);
VALA_EXTERN LxsessionMenuItemObject* lxsession_menu_item_object_construct (GType object_type);
VALA_EXTERN void lxsession_menu_item_object_set_label (LxsessionMenuItemObject* self,
                                           const gchar* s);
static void ____lambda11_ (LxsessionUpdatesManagerApp* self);
static void _____lambda11__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                         gpointer self);
VALA_EXTERN void lxsession_menu_item_object_show (LxsessionMenuItemObject* self);
VALA_EXTERN void lxsession_menu_object_add (LxsessionMenuObject* self,
                                GObject* item);
VALA_EXTERN LxsessionIconObject* lxsession_icon_object_new (const gchar* name_param,
                                                const gchar* icon_name_param,
                                                const gchar* notification_param,
                                                LxsessionMenuObject* menu_param);
VALA_EXTERN LxsessionIconObject* lxsession_icon_object_construct (GType object_type,
                                                      const gchar* name_param,
                                                      const gchar* icon_name_param,
                                                      const gchar* notification_param,
                                                      LxsessionMenuObject* menu_param);
VALA_EXTERN void lxsession_icon_object_init (LxsessionIconObject* self);
VALA_EXTERN void lxsession_icon_object_clear_actions (LxsessionIconObject* self);
VALA_EXTERN void lxsession_icon_object_add_action (LxsessionIconObject* self,
                                       const gchar* action,
                                       const gchar* label,
                                       LxsessionIconObjectActionCallback callback,
                                       gpointer callback_target,
                                       GDestroyNotify callback_target_destroy_notify);
static void ____lambda12_ (LxsessionUpdatesManagerApp* self);
static void _____lambda12__lxsession_icon_object_action_callback (gpointer self);
VALA_EXTERN void lxsession_updates_manager_app_language_launch (LxsessionUpdatesManagerApp* self);
VALA_EXTERN void lxsession_updates_manager_app_check_language_support (LxsessionUpdatesManagerApp* self);
static gboolean lxsession_updates_manager_app_check_language_support_sync (LxsessionUpdatesManagerApp* self);
static gboolean _lxsession_updates_manager_app_check_language_support_sync_gsource_func (gpointer self);
static void _____lambda9_ (LxsessionUpdatesManagerApp* self);
static void ______lambda9__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                         gpointer self);
static void _____lambda10_ (LxsessionUpdatesManagerApp* self);
static void ______lambda10__lxsession_icon_object_action_callback (gpointer self);
VALA_EXTERN void lxsession_updates_manager_app_upgrade_launch (LxsessionUpdatesManagerApp* self,
                                                   const gchar* upgrade_manager_command);
static Block1Data* block1_data_ref (Block1Data* _data1_);
static void block1_data_unref (void * _userdata_);
static void ____lambda5_ (Block1Data* _data1_);
static void _____lambda5__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                        gpointer self);
static void ____lambda6_ (Block1Data* _data1_);
static void _____lambda6__lxsession_icon_object_action_callback (gpointer self);
VALA_EXTERN void lxsession_icon_object_set_notification_body (LxsessionIconObject* self,
                                                  const gchar* text);
VALA_EXTERN void lxsession_icon_object_set_icon (LxsessionIconObject* self,
                                     const gchar* param_icon_name);
static void ___lambda7_ (LxsessionUpdatesManagerApp* self);
static void ____lambda7__lxsession_icon_object_action_callback (gpointer self);
static void ___lambda8_ (LxsessionUpdatesManagerApp* self);
static void ____lambda8__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                       gpointer self);
VALA_EXTERN void lxsession_icon_object_set_menu (LxsessionIconObject* self,
                                     LxsessionMenuObject* param_menu);
static void _lxsession_updates_manager_app_run_check_g_file_monitor_changed (GFileMonitor* _sender,
                                                                      GFile* file,
                                                                      GFile* other_file,
                                                                      GFileMonitorEvent event_type,
                                                                      gpointer self);
static void _lxsession_updates_manager_app_run_check_reboot_g_file_monitor_changed (GFileMonitor* _sender,
                                                                             GFile* file,
                                                                             GFile* other_file,
                                                                             GFileMonitorEvent event_type,
                                                                             gpointer self);
static void lxsession_updates_manager_app_finalize (GObject * obj);
static GType lxsession_updates_manager_app_get_type_once (void);
VALA_EXTERN GType lxsession_crash_manager_app_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (LxsessionCrashManagerApp, g_object_unref)
VALA_EXTERN LxsessionCrashManagerApp* lxsession_crash_manager_app_new (void);
VALA_EXTERN LxsessionCrashManagerApp* lxsession_crash_manager_app_construct (GType object_type);
static void lxsession_crash_manager_app_real_read_settings (LxsessionAppObject* base);
VALA_EXTERN void lxsession_crash_manager_app_setup_crash_log_config (LxsessionCrashManagerApp* self);
VALA_EXTERN void lxsession_crash_manager_app_run_crash_check (LxsessionCrashManagerApp* self);
VALA_EXTERN void lxsession_crash_manager_app_test_activate (LxsessionCrashManagerApp* self);
VALA_EXTERN void lxsession_crash_manager_app_test_inactivate (LxsessionCrashManagerApp* self);
static void _lxsession_crash_manager_app_run_crash_check_g_file_monitor_changed (GFileMonitor* _sender,
                                                                          GFile* file,
                                                                          GFile* other_file,
                                                                          GFileMonitorEvent event_type,
                                                                          gpointer self);
VALA_EXTERN gboolean lxsession_crash_manager_app_on_crash_file_change (LxsessionCrashManagerApp* self);
static gboolean _lxsession_crash_manager_app_on_crash_file_change_gsource_func (gpointer self);
VALA_EXTERN GList* lxsession_crash_manager_app_check_crash_file (LxsessionCrashManagerApp* self);
static void _vala_array_add1 (gchar** * array,
                       gint* length,
                       gint* size,
                       gchar* value);
static inline void _g_list_free__g_free0_ (GList* self);
static gboolean _vala_string_array_contains (gchar* * stack,
                                      gssize stack_length,
                                      const gchar* needle);
VALA_EXTERN void lxsession_crash_manager_app_crash_report_launch (LxsessionCrashManagerApp* self,
                                                      const gchar* command);
VALA_EXTERN LxsessionMenuObject* lxsession_crash_manager_app_build_menu (LxsessionCrashManagerApp* self,
                                                             GList* crash_list);
static Block2Data* block2_data_ref (Block2Data* _data2_);
static void block2_data_unref (void * _userdata_);
static void _____lambda13_ (Block2Data* _data2_);
static void ______lambda13__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                          gpointer self);
static Block3Data* block3_data_ref (Block3Data* _data3_);
static void block3_data_unref (void * _userdata_);
static void ____lambda14_ (Block3Data* _data3_);
static void _____lambda14__lxsession_icon_object_action_callback (gpointer self);
static void lxsession_crash_manager_app_finalize (GObject * obj);
static GType lxsession_crash_manager_app_get_type_once (void);
static void _vala_array_destroy (gpointer array,
                          gssize array_length,
                          GDestroyNotify destroy_func);
static void _vala_array_free (gpointer array,
                       gssize array_length,
                       GDestroyNotify destroy_func);
static gssize _vala_array_length (gpointer array);

static inline gpointer
lxsession_app_object_get_instance_private (LxsessionAppObject* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionAppObject_private_offset);
}

LxsessionAppObject*
lxsession_app_object_construct (GType object_type)
{
	LxsessionAppObject * self = NULL;
	self = (LxsessionAppObject*) g_object_new (object_type, NULL);
	lxsession_app_object_init (self);
	return self;
}

LxsessionAppObject*
lxsession_app_object_new (void)
{
	return lxsession_app_object_construct (LXSESSION_TYPE_APP_OBJECT);
}

void
lxsession_app_object_launch (LxsessionAppObject* self)
{
	g_return_if_fail (self != NULL);
	lxsession_app_object_generic_launch (self, NULL);
}

static void
_lxsession_app_object_callback_pid_gchild_watch_func (GPid pid,
                                                      gint wait_status,
                                                      gpointer self)
{
	lxsession_app_object_callback_pid ((LxsessionAppObject*) self, pid, wait_status);
}

static gboolean
lxsession_app_object_restart_after_backoff (gpointer data)
{
	LxsessionAppObject *self = data;
	self->restart_source_id = 0;
	if (self->pid == 0 && !self->intentional_stop && !self->reload_pending) {
		if (LXSESSION_IS_WINDOWS_MANAGER_APP (self))
			lxsession_windows_manager_app_launch ((LxsessionWindowsManagerApp *)self);
		else
			lxsession_app_object_launch (self);
	}
	return G_SOURCE_REMOVE;
}

void
lxsession_app_object_generic_launch (LxsessionAppObject* self,
                                     const gchar* arg1)
{
	const gchar* _tmp0_;
	GError* _inner_error0_ = NULL;
	g_return_if_fail (self != NULL);
	_tmp0_ = self->priv->_name;
	if (_tmp0_ != NULL) {
		const gchar* _tmp1_;
		_tmp1_ = self->priv->_name;
		if (g_strcmp0 (_tmp1_, "") != 0) {
			{
				gchar** spawn_env = NULL;
				gchar** _tmp2_;
				gchar** _tmp3_;
				gint spawn_env_length1;
				gint _spawn_env_size_;
				gchar** _tmp4_;
				gint _tmp4__length1;
				gint _tmp5_ = 0;
				gchar** _tmp6_;
				gint _tmp6__length1;
				gchar** _tmp7_;
				gint _tmp7__length1;
				GPid _tmp8_ = 0;
				const gchar* _tmp9_;
				FILE* _tmp20_;
				_tmp3_ = _tmp2_ = g_get_environ ();
				spawn_env = _tmp3_;
				spawn_env_length1 = _vala_array_length (_tmp2_);
				_spawn_env_size_ = spawn_env_length1;
				_tmp4_ = lxsession_app_object_get_command (self, &_tmp5_);
				_tmp4__length1 = _tmp5_;
				_tmp6_ = _tmp4_;
				_tmp6__length1 = _tmp4__length1;
				_tmp7_ = spawn_env;
				_tmp7__length1 = spawn_env_length1;
				g_spawn_async (arg1, _tmp6_, _tmp7_, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &_tmp8_, &_inner_error0_);
				self->pid = _tmp8_;
				self->launch_time_us = g_get_monotonic_time ();
				if (G_UNLIKELY (_inner_error0_ != NULL)) {
					spawn_env = (_vala_array_free (spawn_env, spawn_env_length1, (GDestroyNotify) g_free), NULL);
					if (_inner_error0_->domain == G_SPAWN_ERROR) {
						goto __catch0_g_spawn_error;
					}
					g_critical ("file %s: line %d: unexpected error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
					g_clear_error (&_inner_error0_);
					return;
				}
				g_child_watch_add_full (G_PRIORITY_DEFAULT_IDLE, self->pid, _lxsession_app_object_callback_pid_gchild_watch_func, g_object_ref (self), g_object_unref);
				_tmp9_ = self->priv->_name;
				g_message ("app.vala:76: Launching %s ", _tmp9_);
				spawn_env = (_vala_array_free (spawn_env, spawn_env_length1, (GDestroyNotify) g_free), NULL);
			}
			goto __finally0;
			__catch0_g_spawn_error:
			{
				GError* err = NULL;
				GError* _tmp21_;
				const gchar* _tmp22_;
				const gchar* _tmp23_;
				err = _inner_error0_;
				_inner_error0_ = NULL;
				_tmp21_ = err;
				_tmp22_ = _tmp21_->message;
				g_warning ("app.vala:86: %s", _tmp22_);
				_tmp23_ = self->priv->_name;
				g_warning ("app.vala:87: Error when launching %s", _tmp23_);
				_g_error_free0 (err);
			}
			__finally0:
			if (G_UNLIKELY (_inner_error0_ != NULL)) {
				g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
				g_clear_error (&_inner_error0_);
				return;
			}
		}
	}
}

static void
lxsession_app_object_real_read_config_settings (LxsessionAppObject* self)
{
}

void
lxsession_app_object_read_config_settings (LxsessionAppObject* self)
{
	LxsessionAppObjectClass* _klass_;
	g_return_if_fail (self != NULL);
	_klass_ = LXSESSION_APP_OBJECT_GET_CLASS (self);
	if (_klass_->read_config_settings) {
		_klass_->read_config_settings (self);
	}
}

static void
lxsession_app_object_real_read_settings (LxsessionAppObject* self)
{
}

void
lxsession_app_object_read_settings (LxsessionAppObject* self)
{
	LxsessionAppObjectClass* _klass_;
	g_return_if_fail (self != NULL);
	_klass_ = LXSESSION_APP_OBJECT_GET_CLASS (self);
	if (_klass_->read_settings) {
		_klass_->read_settings (self);
	}
}

void
lxsession_app_object_stop (LxsessionAppObject* self)
{
	g_return_if_fail (self != NULL);
	if (((gint) self->pid) > 1) {
		g_message ("app.vala:107: Stopping process with pid %d", (gint) self->pid);
		self->intentional_stop = TRUE;
		if (kill ((pid_t) ((gint) self->pid), 15) != 0)
			self->intentional_stop = FALSE;
	}
	if (self->restart_source_id != 0) {
		g_source_remove (self->restart_source_id);
		self->restart_source_id = 0;
	}
}

void
lxsession_app_object_reload (LxsessionAppObject* self)
{
	g_return_if_fail (self != NULL);
	g_message ("app.vala:114: Reloading process");
	if (self->restart_source_id != 0) {
		g_source_remove (self->restart_source_id);
		self->restart_source_id = 0;
	}
	if (self->pid > 1) {
		self->reload_pending = TRUE;
		lxsession_app_object_stop (self);
	} else {
		lxsession_app_object_launch (self);
	}
}

void
lxsession_app_object_init (LxsessionAppObject* self)
{
	g_return_if_fail (self != NULL);
	self->intentional_stop = FALSE;
	self->reload_pending = FALSE;
	self->restart_source_id = 0;
	self->launch_time_us = 0;
	lxsession_app_object_read_config_settings (self);
	lxsession_app_object_read_settings (self);
}

static void
lxsession_app_object_callback_pid (LxsessionAppObject* self,
                                   GPid pid,
                                   gint status)
{
	const gchar* _tmp0_;
	gboolean _tmp1_;
	g_return_if_fail (self != NULL);
	_tmp0_ = self->priv->_name;
	g_message ("app.vala:130: %s exit with this type of exit: %i", _tmp0_, status);
	gboolean was_reload;
	g_spawn_close_pid (pid);
	was_reload = self->reload_pending;
	if (self->pid == pid) {
		self->pid = 0;
		self->reload_pending = FALSE;
		self->intentional_stop = FALSE;
	}
	if (was_reload) {
		lxsession_app_object_launch (self);
		return;
	}
	_tmp1_ = self->priv->_guard;
	if (_tmp1_ == TRUE) {
		/* waitpid-style status: decode with the W* macros (mirrors app.vala;
		 * raw exit code N arrives as N << 8, so never switch on status). */
		gint exit_code = -1;
		gint death_signal = -1;
		if (WIFEXITED (status)) {
			exit_code = WEXITSTATUS (status);
		} else if (WIFSIGNALED (status)) {
			death_signal = WTERMSIG (status);
		}
		if (exit_code == 0) {
			g_message ("app.vala:139: Exit normal, don't reload");
		} else if (death_signal == 15) {
			g_message ("app.vala:142: Exit by the user, don't reload");
		} else {
			gint _tmp2_;
			gint _tmp3_;
			gint _tmp4_;
			g_message ("app.vala:148: Exit not normal, try to reload");
			if (self->launch_time_us > 0 &&
			    g_get_monotonic_time () - self->launch_time_us >= LUXESESSION_HEALTHY_RUNTIME_USEC)
				lxsession_app_object_set_crash_count (self, 0);
			_tmp2_ = self->priv->_crash_count;
			lxsession_app_object_set_crash_count (self, _tmp2_ + 1);
			_tmp3_ = self->priv->_crash_count;
			_tmp4_ = self->priv->_stop_reload;
			if (_tmp3_ <= _tmp4_) {
				guint delay = lxsession_restart_delay_seconds ((guint)_tmp3_);
				self->restart_source_id = g_timeout_add_seconds_full (
					G_PRIORITY_DEFAULT, delay,
					lxsession_app_object_restart_after_backoff,
					g_object_ref (self), g_object_unref);
			} else {
				g_message ("app.vala:156: Application crashed too much, stop reloading");
			}
		}
	}
}

const gchar*
lxsession_app_object_get_name (LxsessionAppObject* self)
{
	const gchar* result;
	const gchar* _tmp0_;
	g_return_val_if_fail (self != NULL, NULL);
	_tmp0_ = self->priv->_name;
	result = _tmp0_;
	return result;
}

void
lxsession_app_object_set_name (LxsessionAppObject* self,
                               const gchar* value)
{
	gchar* old_value;
	g_return_if_fail (self != NULL);
	old_value = lxsession_app_object_get_name (self);
	if (g_strcmp0 (value, old_value) != 0) {
		gchar* _tmp0_;
		_tmp0_ = g_strdup (value);
		_g_free0 (self->priv->_name);
		self->priv->_name = _tmp0_;
		g_object_notify_by_pspec ((GObject *) self, lxsession_app_object_properties[LXSESSION_APP_OBJECT_NAME_PROPERTY]);
	}
}

gchar**
lxsession_app_object_get_command (LxsessionAppObject* self,
                                  gint* result_length1)
{
	gchar** result;
	gchar** _tmp0_;
	gint _tmp0__length1;
	gchar** _tmp1_;
	gint _tmp1__length1;
	g_return_val_if_fail (self != NULL, NULL);
	_tmp0_ = self->priv->_command;
	_tmp0__length1 = self->priv->_command_length1;
	_tmp1_ = _tmp0_;
	_tmp1__length1 = _tmp0__length1;
	if (result_length1) {
		*result_length1 = _tmp1__length1;
	}
	result = _tmp1_;
	return result;
}

static gchar**
_vala_array_dup1 (gchar** self,
                  gssize length)
{
	if (length >= 0) {
		gchar** result;
		gssize i;
		result = g_new0 (gchar*, length + 1);
		for (i = 0; i < length; i++) {
			gchar* _tmp0_;
			_tmp0_ = g_strdup (self[i]);
			result[i] = _tmp0_;
		}
		return result;
	}
	return NULL;
}

void
lxsession_app_object_set_command (LxsessionAppObject* self,
                                  gchar** value,
                                  gint value_length1)
{
	gchar** old_value;
	gint old_value_length;
	g_return_if_fail (self != NULL);
	old_value = lxsession_app_object_get_command (self, &old_value_length);
	if (old_value != value) {
		gchar** _tmp0_;
		gint _tmp0__length1;
		_tmp0_ = (value != NULL) ? _vala_array_dup1 (value, value_length1) : value;
		_tmp0__length1 = value_length1;
		self->priv->_command = (_vala_array_free (self->priv->_command, self->priv->_command_length1, (GDestroyNotify) g_free), NULL);
		self->priv->_command = _tmp0_;
		self->priv->_command_length1 = _tmp0__length1;
		self->priv->__command_size_ = self->priv->_command_length1;
		g_object_notify_by_pspec ((GObject *) self, lxsession_app_object_properties[LXSESSION_APP_OBJECT_COMMAND_PROPERTY]);
	}
}

gboolean
lxsession_app_object_get_guard (LxsessionAppObject* self)
{
	gboolean result;
	g_return_val_if_fail (self != NULL, FALSE);
	result = self->priv->_guard;
	return result;
}

void
lxsession_app_object_set_guard (LxsessionAppObject* self,
                                gboolean value)
{
	gboolean old_value;
	g_return_if_fail (self != NULL);
	old_value = lxsession_app_object_get_guard (self);
	if (old_value != value) {
		self->priv->_guard = value;
		g_object_notify_by_pspec ((GObject *) self, lxsession_app_object_properties[LXSESSION_APP_OBJECT_GUARD_PROPERTY]);
	}
}

const gchar*
lxsession_app_object_get_application_type (LxsessionAppObject* self)
{
	const gchar* result;
	const gchar* _tmp0_;
	g_return_val_if_fail (self != NULL, NULL);
	_tmp0_ = self->priv->_application_type;
	result = _tmp0_;
	return result;
}

void
lxsession_app_object_set_application_type (LxsessionAppObject* self,
                                           const gchar* value)
{
	gchar* old_value;
	g_return_if_fail (self != NULL);
	old_value = lxsession_app_object_get_application_type (self);
	if (g_strcmp0 (value, old_value) != 0) {
		gchar* _tmp0_;
		_tmp0_ = g_strdup (value);
		_g_free0 (self->priv->_application_type);
		self->priv->_application_type = _tmp0_;
		g_object_notify_by_pspec ((GObject *) self, lxsession_app_object_properties[LXSESSION_APP_OBJECT_APPLICATION_TYPE_PROPERTY]);
	}
}

gint
lxsession_app_object_get_crash_count (LxsessionAppObject* self)
{
	gint result;
	g_return_val_if_fail (self != NULL, 0);
	result = self->priv->_crash_count;
	return result;
}

void
lxsession_app_object_set_crash_count (LxsessionAppObject* self,
                                      gint value)
{
	gint old_value;
	g_return_if_fail (self != NULL);
	old_value = lxsession_app_object_get_crash_count (self);
	if (old_value != value) {
		self->priv->_crash_count = value;
		g_object_notify_by_pspec ((GObject *) self, lxsession_app_object_properties[LXSESSION_APP_OBJECT_CRASH_COUNT_PROPERTY]);
	}
}

gint
lxsession_app_object_get_stop_reload (LxsessionAppObject* self)
{
	gint result;
	g_return_val_if_fail (self != NULL, 0);
	result = self->priv->_stop_reload;
	return result;
}

void
lxsession_app_object_set_stop_reload (LxsessionAppObject* self,
                                      gint value)
{
	gint old_value;
	g_return_if_fail (self != NULL);
	old_value = lxsession_app_object_get_stop_reload (self);
	if (old_value != value) {
		self->priv->_stop_reload = value;
		g_object_notify_by_pspec ((GObject *) self, lxsession_app_object_properties[LXSESSION_APP_OBJECT_STOP_RELOAD_PROPERTY]);
	}
}

static void
lxsession_app_object_class_init (LxsessionAppObjectClass * klass,
                                 gpointer klass_data)
{
	lxsession_app_object_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionAppObject_private_offset);
	((LxsessionAppObjectClass *) klass)->read_config_settings = (void (*) (LxsessionAppObject*)) lxsession_app_object_real_read_config_settings;
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_app_object_real_read_settings;
	G_OBJECT_CLASS (klass)->get_property = _vala_lxsession_app_object_get_property;
	G_OBJECT_CLASS (klass)->set_property = _vala_lxsession_app_object_set_property;
	G_OBJECT_CLASS (klass)->finalize = lxsession_app_object_finalize;
	g_object_class_install_property (G_OBJECT_CLASS (klass), LXSESSION_APP_OBJECT_NAME_PROPERTY, lxsession_app_object_properties[LXSESSION_APP_OBJECT_NAME_PROPERTY] = g_param_spec_string ("name", "name", "name", NULL, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_WRITABLE));
	g_object_class_install_property (G_OBJECT_CLASS (klass), LXSESSION_APP_OBJECT_COMMAND_PROPERTY, lxsession_app_object_properties[LXSESSION_APP_OBJECT_COMMAND_PROPERTY] = g_param_spec_boxed ("command", "command", "command", G_TYPE_STRV, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_WRITABLE));
	g_object_class_install_property (G_OBJECT_CLASS (klass), LXSESSION_APP_OBJECT_GUARD_PROPERTY, lxsession_app_object_properties[LXSESSION_APP_OBJECT_GUARD_PROPERTY] = g_param_spec_boolean ("guard", "guard", "guard", FALSE, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_WRITABLE));
	g_object_class_install_property (G_OBJECT_CLASS (klass), LXSESSION_APP_OBJECT_APPLICATION_TYPE_PROPERTY, lxsession_app_object_properties[LXSESSION_APP_OBJECT_APPLICATION_TYPE_PROPERTY] = g_param_spec_string ("application-type", "application-type", "application-type", NULL, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_WRITABLE));
	g_object_class_install_property (G_OBJECT_CLASS (klass), LXSESSION_APP_OBJECT_CRASH_COUNT_PROPERTY, lxsession_app_object_properties[LXSESSION_APP_OBJECT_CRASH_COUNT_PROPERTY] = g_param_spec_int ("crash-count", "crash-count", "crash-count", G_MININT, G_MAXINT, 0, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_WRITABLE));
	g_object_class_install_property (G_OBJECT_CLASS (klass), LXSESSION_APP_OBJECT_STOP_RELOAD_PROPERTY, lxsession_app_object_properties[LXSESSION_APP_OBJECT_STOP_RELOAD_PROPERTY] = g_param_spec_int ("stop-reload", "stop-reload", "stop-reload", G_MININT, G_MAXINT, 5, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_WRITABLE));
}

static void
lxsession_app_object_instance_init (LxsessionAppObject * self,
                                    gpointer klass)
{
	self->priv = lxsession_app_object_get_instance_private (self);
	self->priv->_guard = FALSE;
	self->priv->_crash_count = 0;
	self->priv->_stop_reload = 5;
}

static void
lxsession_app_object_finalize (GObject * obj)
{
	LxsessionAppObject * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_APP_OBJECT, LxsessionAppObject);
	_g_free0 (self->priv->_name);
	self->priv->_command = (_vala_array_free (self->priv->_command, self->priv->_command_length1, (GDestroyNotify) g_free), NULL);
	_g_free0 (self->priv->_application_type);
	G_OBJECT_CLASS (lxsession_app_object_parent_class)->finalize (obj);
}

static GType
lxsession_app_object_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionAppObjectClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_app_object_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionAppObject), 0, (GInstanceInitFunc) lxsession_app_object_instance_init, NULL };
	GType lxsession_app_object_type_id;
	lxsession_app_object_type_id = g_type_register_static (G_TYPE_OBJECT, "LxsessionAppObject", &g_define_type_info, 0);
	LxsessionAppObject_private_offset = g_type_add_instance_private (lxsession_app_object_type_id, sizeof (LxsessionAppObjectPrivate));
	return lxsession_app_object_type_id;
}

GType
lxsession_app_object_get_type (void)
{
	static volatile gsize lxsession_app_object_type_id__once = 0;
	if (g_once_init_enter (&lxsession_app_object_type_id__once)) {
		GType lxsession_app_object_type_id;
		lxsession_app_object_type_id = lxsession_app_object_get_type_once ();
		g_once_init_leave (&lxsession_app_object_type_id__once, lxsession_app_object_type_id);
	}
	return lxsession_app_object_type_id__once;
}

static void
_vala_lxsession_app_object_get_property (GObject * object,
                                         guint property_id,
                                         GValue * value,
                                         GParamSpec * pspec)
{
	LxsessionAppObject * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (object, LXSESSION_TYPE_APP_OBJECT, LxsessionAppObject);
	switch (property_id) {
		case LXSESSION_APP_OBJECT_NAME_PROPERTY:
		g_value_set_string (value, lxsession_app_object_get_name (self));
		break;
		case LXSESSION_APP_OBJECT_COMMAND_PROPERTY:
		{
			int length;
			g_value_set_boxed (value, lxsession_app_object_get_command (self, &length));
		}
		break;
		case LXSESSION_APP_OBJECT_GUARD_PROPERTY:
		g_value_set_boolean (value, lxsession_app_object_get_guard (self));
		break;
		case LXSESSION_APP_OBJECT_APPLICATION_TYPE_PROPERTY:
		g_value_set_string (value, lxsession_app_object_get_application_type (self));
		break;
		case LXSESSION_APP_OBJECT_CRASH_COUNT_PROPERTY:
		g_value_set_int (value, lxsession_app_object_get_crash_count (self));
		break;
		case LXSESSION_APP_OBJECT_STOP_RELOAD_PROPERTY:
		g_value_set_int (value, lxsession_app_object_get_stop_reload (self));
		break;
		default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
_vala_lxsession_app_object_set_property (GObject * object,
                                         guint property_id,
                                         const GValue * value,
                                         GParamSpec * pspec)
{
	LxsessionAppObject * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (object, LXSESSION_TYPE_APP_OBJECT, LxsessionAppObject);
	switch (property_id) {
		case LXSESSION_APP_OBJECT_NAME_PROPERTY:
		lxsession_app_object_set_name (self, g_value_get_string (value));
		break;
		case LXSESSION_APP_OBJECT_COMMAND_PROPERTY:
		{
			gpointer boxed;
			boxed = g_value_get_boxed (value);
			lxsession_app_object_set_command (self, boxed, (boxed == NULL) ? 0 : g_strv_length (boxed));
		}
		break;
		case LXSESSION_APP_OBJECT_GUARD_PROPERTY:
		lxsession_app_object_set_guard (self, g_value_get_boolean (value));
		break;
		case LXSESSION_APP_OBJECT_APPLICATION_TYPE_PROPERTY:
		lxsession_app_object_set_application_type (self, g_value_get_string (value));
		break;
		case LXSESSION_APP_OBJECT_CRASH_COUNT_PROPERTY:
		lxsession_app_object_set_crash_count (self, g_value_get_int (value));
		break;
		case LXSESSION_APP_OBJECT_STOP_RELOAD_PROPERTY:
		lxsession_app_object_set_stop_reload (self, g_value_get_int (value));
		break;
		default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

LxsessionSimpleAppObject*
lxsession_simple_app_object_construct (GType object_type)
{
	LxsessionSimpleAppObject * self = NULL;
	gchar* _tmp0_;
	gchar** _tmp1_;
	gchar** _tmp2_;
	gint _tmp2__length1;
	self = (LxsessionSimpleAppObject*) lxsession_app_object_construct (object_type);
	lxsession_app_object_set_name ((LxsessionAppObject*) self, "");
	_tmp0_ = g_strdup ("");
	_tmp1_ = g_new0 (gchar*, 1 + 1);
	_tmp1_[0] = _tmp0_;
	_tmp2_ = _tmp1_;
	_tmp2__length1 = 1;
	lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp2_, 1);
	_tmp2_ = (_vala_array_free (_tmp2_, _tmp2__length1, (GDestroyNotify) g_free), NULL);
	lxsession_app_object_set_guard ((LxsessionAppObject*) self, FALSE);
	lxsession_app_object_set_application_type ((LxsessionAppObject*) self, "");
	return self;
}

LxsessionSimpleAppObject*
lxsession_simple_app_object_new (void)
{
	return lxsession_simple_app_object_construct (LXSESSION_TYPE_SIMPLE_APP_OBJECT);
}

static void
lxsession_simple_app_object_class_init (LxsessionSimpleAppObjectClass * klass,
                                        gpointer klass_data)
{
	lxsession_simple_app_object_parent_class = g_type_class_peek_parent (klass);
}

static void
lxsession_simple_app_object_instance_init (LxsessionSimpleAppObject * self,
                                           gpointer klass)
{
}

static GType
lxsession_simple_app_object_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionSimpleAppObjectClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_simple_app_object_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionSimpleAppObject), 0, (GInstanceInitFunc) lxsession_simple_app_object_instance_init, NULL };
	GType lxsession_simple_app_object_type_id;
	lxsession_simple_app_object_type_id = g_type_register_static (LXSESSION_TYPE_APP_OBJECT, "LxsessionSimpleAppObject", &g_define_type_info, 0);
	return lxsession_simple_app_object_type_id;
}

GType
lxsession_simple_app_object_get_type (void)
{
	static volatile gsize lxsession_simple_app_object_type_id__once = 0;
	if (g_once_init_enter (&lxsession_simple_app_object_type_id__once)) {
		GType lxsession_simple_app_object_type_id;
		lxsession_simple_app_object_type_id = lxsession_simple_app_object_get_type_once ();
		g_once_init_leave (&lxsession_simple_app_object_type_id__once, lxsession_simple_app_object_type_id);
	}
	return lxsession_simple_app_object_type_id__once;
}

LxsessionGenericAppObject*
lxsession_generic_app_object_construct (GType object_type,
                                        LxsessionAppType* app_type)
{
	LxsessionGenericAppObject * self = NULL;
	LxsessionAppType _tmp0_;
	const gchar* _tmp1_;
	LxsessionAppType _tmp2_;
	gchar** _tmp3_;
	gint _tmp3__length1;
	LxsessionAppType _tmp4_;
	LxsessionAppType _tmp5_;
	const gchar* _tmp6_;
	g_return_val_if_fail (app_type != NULL, NULL);
	self = (LxsessionGenericAppObject*) lxsession_app_object_construct (object_type);
	_tmp0_ = *app_type;
	_tmp1_ = _tmp0_.name;
	lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp1_);
	_tmp2_ = *app_type;
	_tmp3_ = _tmp2_.command;
	_tmp3__length1 = _tmp2_.command_length1;
	lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp3_, _tmp3__length1);
	_tmp4_ = *app_type;
	lxsession_app_object_set_guard ((LxsessionAppObject*) self, _tmp4_.guard);
	_tmp5_ = *app_type;
	_tmp6_ = _tmp5_.application_type;
	lxsession_app_object_set_application_type ((LxsessionAppObject*) self, _tmp6_);
	return self;
}

LxsessionGenericAppObject*
lxsession_generic_app_object_new (LxsessionAppType* app_type)
{
	return lxsession_generic_app_object_construct (LXSESSION_TYPE_GENERIC_APP_OBJECT, app_type);
}

static void
lxsession_generic_app_object_class_init (LxsessionGenericAppObjectClass * klass,
                                         gpointer klass_data)
{
	lxsession_generic_app_object_parent_class = g_type_class_peek_parent (klass);
}

static void
lxsession_generic_app_object_instance_init (LxsessionGenericAppObject * self,
                                            gpointer klass)
{
}

static GType
lxsession_generic_app_object_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionGenericAppObjectClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_generic_app_object_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionGenericAppObject), 0, (GInstanceInitFunc) lxsession_generic_app_object_instance_init, NULL };
	GType lxsession_generic_app_object_type_id;
	lxsession_generic_app_object_type_id = g_type_register_static (LXSESSION_TYPE_APP_OBJECT, "LxsessionGenericAppObject", &g_define_type_info, 0);
	return lxsession_generic_app_object_type_id;
}

GType
lxsession_generic_app_object_get_type (void)
{
	static volatile gsize lxsession_generic_app_object_type_id__once = 0;
	if (g_once_init_enter (&lxsession_generic_app_object_type_id__once)) {
		GType lxsession_generic_app_object_type_id;
		lxsession_generic_app_object_type_id = lxsession_generic_app_object_get_type_once ();
		g_once_init_leave (&lxsession_generic_app_object_type_id__once, lxsession_generic_app_object_type_id);
	}
	return lxsession_generic_app_object_type_id__once;
}

static inline gpointer
lxsession_generic_simple_app_get_instance_private (LxsessionGenericSimpleApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionGenericSimpleApp_private_offset);
}

LxsessionGenericSimpleApp*
lxsession_generic_simple_app_construct (GType object_type,
                                        const gchar* argument)
{
	LxsessionGenericSimpleApp * self = NULL;
	g_return_val_if_fail (argument != NULL, NULL);
	self = (LxsessionGenericSimpleApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_generic_simple_app_set_settings_command (self, argument);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionGenericSimpleApp*
lxsession_generic_simple_app_new (const gchar* argument)
{
	return lxsession_generic_simple_app_construct (LXSESSION_TYPE_GENERIC_SIMPLE_APP, argument);
}

static void
lxsession_generic_simple_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionGenericSimpleApp *self = (LxsessionGenericSimpleApp*)base;
	const gchar *settings = self->priv->_settings_command;
	gchar **command = NULL;
	GError *error = NULL;

	if (settings == NULL || settings[0] == '\0' ||
	    !g_shell_parse_argv(settings, NULL, &command, &error) ||
	    command == NULL || command[0] == NULL || command[0][0] == '\0')
	{
		if (error != NULL)
			g_warning("app.vala: invalid generic command: %s", error->message);
		g_clear_error(&error);
		g_strfreev(command);
		lxsession_app_object_set_name((LxsessionAppObject*)self, "");
		return;
	}
	lxsession_app_object_set_name((LxsessionAppObject*)self, command[0]);
	lxsession_app_object_set_command((LxsessionAppObject*)self, command,
	                                  (gint)g_strv_length(command));
	g_strfreev(command);
}

const gchar*
lxsession_generic_simple_app_get_settings_command (LxsessionGenericSimpleApp* self)
{
	const gchar* result;
	const gchar* _tmp0_;
	g_return_val_if_fail (self != NULL, NULL);
	_tmp0_ = self->priv->_settings_command;
	result = _tmp0_;
	return result;
}

void
lxsession_generic_simple_app_set_settings_command (LxsessionGenericSimpleApp* self,
                                                   const gchar* value)
{
	gchar* old_value;
	g_return_if_fail (self != NULL);
	old_value = lxsession_generic_simple_app_get_settings_command (self);
	if (g_strcmp0 (value, old_value) != 0) {
		gchar* _tmp0_;
		_tmp0_ = g_strdup (value);
		_g_free0 (self->priv->_settings_command);
		self->priv->_settings_command = _tmp0_;
		g_object_notify_by_pspec ((GObject *) self, lxsession_generic_simple_app_properties[LXSESSION_GENERIC_SIMPLE_APP_SETTINGS_COMMAND_PROPERTY]);
	}
}

static void
lxsession_generic_simple_app_class_init (LxsessionGenericSimpleAppClass * klass,
                                         gpointer klass_data)
{
	lxsession_generic_simple_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionGenericSimpleApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_generic_simple_app_real_read_settings;
	G_OBJECT_CLASS (klass)->get_property = _vala_lxsession_generic_simple_app_get_property;
	G_OBJECT_CLASS (klass)->set_property = _vala_lxsession_generic_simple_app_set_property;
	G_OBJECT_CLASS (klass)->finalize = lxsession_generic_simple_app_finalize;
	g_object_class_install_property (G_OBJECT_CLASS (klass), LXSESSION_GENERIC_SIMPLE_APP_SETTINGS_COMMAND_PROPERTY, lxsession_generic_simple_app_properties[LXSESSION_GENERIC_SIMPLE_APP_SETTINGS_COMMAND_PROPERTY] = g_param_spec_string ("settings-command", "settings-command", "settings-command", NULL, G_PARAM_STATIC_STRINGS | G_PARAM_READABLE | G_PARAM_WRITABLE));
}

static void
lxsession_generic_simple_app_instance_init (LxsessionGenericSimpleApp * self,
                                            gpointer klass)
{
	gchar* _tmp0_;
	self->priv = lxsession_generic_simple_app_get_instance_private (self);
	_tmp0_ = g_strdup ("");
	self->priv->_settings_command = _tmp0_;
}

static void
lxsession_generic_simple_app_finalize (GObject * obj)
{
	LxsessionGenericSimpleApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_GENERIC_SIMPLE_APP, LxsessionGenericSimpleApp);
	_g_free0 (self->priv->_settings_command);
	G_OBJECT_CLASS (lxsession_generic_simple_app_parent_class)->finalize (obj);
}

static GType
lxsession_generic_simple_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionGenericSimpleAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_generic_simple_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionGenericSimpleApp), 0, (GInstanceInitFunc) lxsession_generic_simple_app_instance_init, NULL };
	GType lxsession_generic_simple_app_type_id;
	lxsession_generic_simple_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionGenericSimpleApp", &g_define_type_info, 0);
	LxsessionGenericSimpleApp_private_offset = g_type_add_instance_private (lxsession_generic_simple_app_type_id, sizeof (LxsessionGenericSimpleAppPrivate));
	return lxsession_generic_simple_app_type_id;
}

GType
lxsession_generic_simple_app_get_type (void)
{
	static volatile gsize lxsession_generic_simple_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_generic_simple_app_type_id__once)) {
		GType lxsession_generic_simple_app_type_id;
		lxsession_generic_simple_app_type_id = lxsession_generic_simple_app_get_type_once ();
		g_once_init_leave (&lxsession_generic_simple_app_type_id__once, lxsession_generic_simple_app_type_id);
	}
	return lxsession_generic_simple_app_type_id__once;
}

static void
_vala_lxsession_generic_simple_app_get_property (GObject * object,
                                                 guint property_id,
                                                 GValue * value,
                                                 GParamSpec * pspec)
{
	LxsessionGenericSimpleApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (object, LXSESSION_TYPE_GENERIC_SIMPLE_APP, LxsessionGenericSimpleApp);
	switch (property_id) {
		case LXSESSION_GENERIC_SIMPLE_APP_SETTINGS_COMMAND_PROPERTY:
		g_value_set_string (value, lxsession_generic_simple_app_get_settings_command (self));
		break;
		default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
_vala_lxsession_generic_simple_app_set_property (GObject * object,
                                                 guint property_id,
                                                 const GValue * value,
                                                 GParamSpec * pspec)
{
	LxsessionGenericSimpleApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (object, LXSESSION_TYPE_GENERIC_SIMPLE_APP, LxsessionGenericSimpleApp);
	switch (property_id) {
		case LXSESSION_GENERIC_SIMPLE_APP_SETTINGS_COMMAND_PROPERTY:
		lxsession_generic_simple_app_set_settings_command (self, g_value_get_string (value));
		break;
		default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static inline gpointer
lxsession_windows_manager_app_get_instance_private (LxsessionWindowsManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionWindowsManagerApp_private_offset);
}

LxsessionWindowsManagerApp*
lxsession_windows_manager_app_construct (GType object_type)
{
	LxsessionWindowsManagerApp * self = NULL;
	self = (LxsessionWindowsManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionWindowsManagerApp*
lxsession_windows_manager_app_new (void)
{
	return lxsession_windows_manager_app_construct (LXSESSION_TYPE_WINDOWS_MANAGER_APP);
}

static void
lxsession_windows_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionWindowsManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	gchar* _tmp2_;
	gboolean _tmp3_;
	gchar* session_command = NULL;
	const gchar* _tmp16_;
	self = (LxsessionWindowsManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "window_manager", NULL);
	_tmp2_ = _tmp1_;
	_tmp3_ = _tmp2_ != NULL;
	_g_free0 (_tmp2_);
	if (_tmp3_) {
		gchar* _tmp4_;
		LxsessionLxsessionConfigKeyFile* _tmp5_;
		gchar* _tmp6_;
		gchar* _tmp7_;
		gchar* _tmp8_;
		_tmp4_ = g_strdup ("simple");
		_g_free0 (self->priv->mode);
		self->priv->mode = _tmp4_;
		_tmp5_ = lxsession_global_settings;
		_tmp6_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp5_, "Session", "window_manager", NULL);
		_g_free0 (self->priv->wm_command);
		self->priv->wm_command = _tmp6_;
		_tmp7_ = g_strdup ("");
		_g_free0 (self->priv->session);
		self->priv->session = _tmp7_;
		_tmp8_ = g_strdup ("");
		_g_free0 (self->priv->extras);
		self->priv->extras = _tmp8_;
	} else {
		gchar* _tmp9_;
		LxsessionLxsessionConfigKeyFile* _tmp10_;
		gchar* _tmp11_;
		LxsessionLxsessionConfigKeyFile* _tmp12_;
		gchar* _tmp13_;
		LxsessionLxsessionConfigKeyFile* _tmp14_;
		gchar* _tmp15_;
		_tmp9_ = g_strdup ("advanced");
		_g_free0 (self->priv->mode);
		self->priv->mode = _tmp9_;
		_tmp10_ = lxsession_global_settings;
		_tmp11_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp10_, "Session", "windows_manager", "command");
		_g_free0 (self->priv->wm_command);
		self->priv->wm_command = _tmp11_;
		_tmp12_ = lxsession_global_settings;
		_tmp13_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp12_, "Session", "windows_manager", "session");
		_g_free0 (self->priv->session);
		self->priv->session = _tmp13_;
		_tmp14_ = lxsession_global_settings;
		_tmp15_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp14_, "Session", "windows_manager", "extras");
		_g_free0 (self->priv->extras);
		self->priv->extras = _tmp15_;
	}
	_tmp16_ = self->priv->wm_command;
	if (g_strcmp0 (_tmp16_, "wm_safe") == 0) {
		gchar* _tmp17_;
		gchar** _tmp18_;
		gchar** _tmp19_;
		gint _tmp19__length1;
		lxsession_app_object_set_name ((LxsessionAppObject*) self, "wm_safe");
		_tmp17_ = lxsession_windows_manager_app_find_window_manager (self);
		_tmp18_ = g_new0 (gchar*, 1 + 1);
		_tmp18_[0] = _tmp17_;
		_tmp19_ = _tmp18_;
		_tmp19__length1 = 1;
		lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp19_, 1);
		_tmp19_ = (_vala_array_free (_tmp19_, _tmp19__length1, (GDestroyNotify) g_free), NULL);
	} else {
		const gchar* _tmp20_;
		_tmp20_ = self->priv->mode;
		if (g_strcmp0 (_tmp20_, "simple") == 0) {
			const gchar* _tmp21_;
			const gchar* _tmp22_;
			gchar* _tmp23_;
			gchar** _tmp24_;
			gchar** _tmp25_;
			gint _tmp25__length1;
			_tmp21_ = self->priv->wm_command;
			lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp21_);
			_tmp22_ = self->priv->wm_command;
			_tmp23_ = g_strdup (_tmp22_);
			_tmp24_ = g_new0 (gchar*, 1 + 1);
			_tmp24_[0] = _tmp23_;
			_tmp25_ = _tmp24_;
			_tmp25__length1 = 1;
			lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp25_, 1);
			_tmp25_ = (_vala_array_free (_tmp25_, _tmp25__length1, (GDestroyNotify) g_free), NULL);
		} else {
			const gchar* _tmp26_;
			gchar* create_command = NULL;
			gchar* xdg_config_env = NULL;
			const gchar* _tmp27_;
			gchar* _tmp28_;
			const gchar* _tmp29_;
			const gchar* _tmp30_;
			GQuark _tmp32_ = 0U;
			static GQuark _tmp31_label0 = 0;
			static GQuark _tmp31_label1 = 0;
			const gchar* _tmp52_;
			const gchar* _tmp53_;
			GQuark _tmp55_ = 0U;
			static GQuark _tmp54_label0 = 0;
			static GQuark _tmp54_label1 = 0;
			static GQuark _tmp54_label2 = 0;
			const gchar* _tmp73_;
			gchar** _tmp74_;
			gchar** _tmp75_;
			gchar** _tmp76_;
			gint _tmp76__length1;
			_tmp26_ = self->priv->wm_command;
			lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp26_);
			_tmp27_ = g_getenv ("XDG_CONFIG_HOME");
			_tmp28_ = g_strdup (_tmp27_);
			xdg_config_env = _tmp28_;
			_tmp29_ = self->priv->wm_command;
			_tmp30_ = _tmp29_;
			_tmp32_ = (NULL == _tmp30_) ? 0 : g_quark_from_string (_tmp30_);
			if (_tmp32_ == ((0 != _tmp31_label0) ? _tmp31_label0 : (_tmp31_label0 = g_quark_from_static_string ("openbox")))) {
				switch (0) {
					default:
					{
						const gchar* _tmp33_;
						const gchar* _tmp34_;
						GQuark _tmp36_ = 0U;
						static GQuark _tmp35_label0 = 0;
						static GQuark _tmp35_label1 = 0;
						_tmp33_ = self->priv->session;
						_tmp34_ = _tmp33_;
						_tmp36_ = (NULL == _tmp34_) ? 0 : g_quark_from_string (_tmp34_);
						if (_tmp36_ == ((0 != _tmp35_label0) ? _tmp35_label0 : (_tmp35_label0 = g_quark_from_static_string ("LXDE")))) {
							switch (0) {
								default:
								{
									const gchar* _tmp37_;
									gchar* _tmp38_;
									gchar* _tmp39_;
									gchar* _tmp40_;
									_tmp37_ = xdg_config_env;
									_tmp38_ = g_strconcat (" --config-file ", _tmp37_, NULL);
									_tmp39_ = _tmp38_;
									_tmp40_ = g_strconcat (_tmp39_, "/openbox/lxde-rc.xml", NULL);
									_g_free0 (session_command);
									session_command = _tmp40_;
									_g_free0 (_tmp39_);
									break;
								}
							}
						} else if (_tmp36_ == ((0 != _tmp35_label1) ? _tmp35_label1 : (_tmp35_label1 = g_quark_from_static_string ("Lubuntu")))) {
							switch (0) {
								default:
								{
									const gchar* _tmp41_;
									gchar* _tmp42_;
									gchar* _tmp43_;
									gchar* _tmp44_;
									_tmp41_ = xdg_config_env;
									_tmp42_ = g_strconcat (" --config-file ", _tmp41_, NULL);
									_tmp43_ = _tmp42_;
									_tmp44_ = g_strconcat (_tmp43_, "/openbox/lubuntu-rc.xml", NULL);
									_g_free0 (session_command);
									session_command = _tmp44_;
									_g_free0 (_tmp43_);
									break;
								}
							}
						} else {
							switch (0) {
								default:
								{
									gchar* _tmp45_;
									_tmp45_ = g_strdup (" ");
									_g_free0 (session_command);
									session_command = _tmp45_;
									break;
								}
							}
						}
						break;
					}
				}
			} else if (_tmp32_ == ((0 != _tmp31_label1) ? _tmp31_label1 : (_tmp31_label1 = g_quark_from_static_string ("openbox-custom")))) {
				switch (0) {
					default:
					{
						const gchar* _tmp46_;
						const gchar* _tmp47_;
						GQuark _tmp49_ = 0U;
						_tmp46_ = self->priv->session;
						_tmp47_ = _tmp46_;
						_tmp49_ = (NULL == _tmp47_) ? 0 : g_quark_from_string (_tmp47_);
						switch (0) {
							default:
							{
								const gchar* _tmp50_;
								gchar* _tmp51_;
								_tmp50_ = self->priv->session;
								_tmp51_ = g_strconcat (" --config-file ", _tmp50_, NULL);
								_g_free0 (session_command);
								session_command = _tmp51_;
								break;
							}
						}
						break;
					}
				}
			} else {
				switch (0) {
					default:
					{
						_g_free0 (session_command);
						session_command = NULL;
						break;
					}
				}
			}
			_tmp52_ = self->priv->extras;
			_tmp53_ = _tmp52_;
			_tmp55_ = (NULL == _tmp53_) ? 0 : g_quark_from_string (_tmp53_);
			if (_tmp55_ == ((0 != _tmp54_label0) ? _tmp54_label0 : (_tmp54_label0 = g_quark_from_static_string (NULL)))) {
				switch (0) {
					default:
					{
						const gchar* _tmp56_;
						const gchar* _tmp57_;
						gchar* _tmp58_;
						_tmp56_ = self->priv->wm_command;
						_tmp57_ = session_command;
						_tmp58_ = g_strconcat (_tmp56_, _tmp57_, NULL);
						_g_free0 (create_command);
						create_command = _tmp58_;
						break;
					}
				}
			} else if (_tmp55_ == ((0 != _tmp54_label1) ? _tmp54_label1 : (_tmp54_label1 = g_quark_from_static_string ("")))) {
				switch (0) {
					default:
					{
						const gchar* _tmp59_;
						const gchar* _tmp60_;
						gchar* _tmp61_;
						_tmp59_ = self->priv->wm_command;
						_tmp60_ = session_command;
						_tmp61_ = g_strconcat (_tmp59_, _tmp60_, NULL);
						_g_free0 (create_command);
						create_command = _tmp61_;
						break;
					}
				}
			} else if (_tmp55_ == ((0 != _tmp54_label2) ? _tmp54_label2 : (_tmp54_label2 = g_quark_from_static_string (" ")))) {
				switch (0) {
					default:
					{
						const gchar* _tmp62_;
						const gchar* _tmp63_;
						gchar* _tmp64_;
						_tmp62_ = self->priv->wm_command;
						_tmp63_ = session_command;
						_tmp64_ = g_strconcat (_tmp62_, _tmp63_, NULL);
						_g_free0 (create_command);
						create_command = _tmp64_;
						break;
					}
				}
			} else {
				switch (0) {
					default:
					{
						const gchar* _tmp65_;
						const gchar* _tmp66_;
						gchar* _tmp67_;
						gchar* _tmp68_;
						gchar* _tmp69_;
						gchar* _tmp70_;
						const gchar* _tmp71_;
						gchar* _tmp72_;
						_tmp65_ = self->priv->wm_command;
						_tmp66_ = session_command;
						_tmp67_ = g_strconcat (_tmp65_, _tmp66_, NULL);
						_tmp68_ = _tmp67_;
						_tmp69_ = g_strconcat (_tmp68_, " ", NULL);
						_tmp70_ = _tmp69_;
						_tmp71_ = self->priv->extras;
						_tmp72_ = g_strconcat (_tmp70_, _tmp71_, NULL);
						_g_free0 (create_command);
						create_command = _tmp72_;
						_g_free0 (_tmp70_);
						_g_free0 (_tmp68_);
						break;
					}
				}
			}
			_tmp73_ = create_command;
			_tmp75_ = _tmp74_ = NULL;
			{
				gchar **parsed = NULL;
				GError *parse_error = NULL;
				if (g_shell_parse_argv(create_command, NULL, &parsed, &parse_error) &&
				    parsed != NULL && parsed[0] != NULL && parsed[0][0] != '\0')
				{
					lxsession_app_object_set_command((LxsessionAppObject*)self,
					                                   parsed,
					                                   (gint)g_strv_length(parsed));
				}
				else if (parse_error != NULL)
				{
					g_warning("app.vala: invalid window-manager command: %s",
					          parse_error->message);
				}
				g_clear_error(&parse_error);
				g_strfreev(parsed);
			}
			_g_free0 (xdg_config_env);
			_g_free0 (create_command);
		}
	}
	lxsession_app_object_set_guard ((LxsessionAppObject*) self, TRUE);
	_g_free0 (session_command);
}

static void
_g_free0_ (gpointer var)
{
	var = (g_free (var), NULL);
}

static void
_vala_gchar_free_function_content_of (gpointer data)
{
	gchar* self;
	self = *((gchar**) data);
	_g_free0_ (self);
}

static gchar*
lxsession_windows_manager_app_find_window_manager (LxsessionWindowsManagerApp* self)
{
	GArray* wm_list = NULL;
	GArray* _tmp0_;
	GArray* _tmp1_;
	gchar* _tmp2_;
	GArray* _tmp3_;
	gchar* _tmp4_;
	GArray* _tmp5_;
	gchar* _tmp6_;
	GArray* _tmp7_;
	gchar* _tmp8_;
	GArray* _tmp9_;
	gchar* _tmp10_;
	GArray* _tmp11_;
	gchar* _tmp12_;
	GArray* _tmp13_;
	gchar* _tmp14_;
	GArray* _tmp15_;
	gchar* _tmp16_;
	GArray* _tmp17_;
	gchar* _tmp18_;
	GArray* _tmp19_;
	gchar* _tmp20_;
	gchar* return_value = NULL;
	gchar* _tmp21_;
	gchar* result;
	g_return_val_if_fail (self != NULL, NULL);
	_tmp0_ = g_array_new (TRUE, TRUE, sizeof (gchar*));
	g_array_set_clear_func (_tmp0_, (GDestroyNotify) _vala_gchar_free_function_content_of);
	wm_list = _tmp0_;
	_tmp1_ = wm_list;
	_tmp2_ = g_strdup ("openbox-lxde");
	g_array_append_val (_tmp1_, _tmp2_);
	_tmp3_ = wm_list;
	_tmp4_ = g_strdup ("openbox-lubuntu");
	g_array_append_val (_tmp3_, _tmp4_);
	_tmp5_ = wm_list;
	_tmp6_ = g_strdup ("openbox");
	g_array_append_val (_tmp5_, _tmp6_);
	_tmp7_ = wm_list;
	_tmp8_ = g_strdup ("compiz");
	g_array_append_val (_tmp7_, _tmp8_);
	_tmp9_ = wm_list;
	_tmp10_ = g_strdup ("kwin");
	g_array_append_val (_tmp9_, _tmp10_);
	_tmp11_ = wm_list;
	_tmp12_ = g_strdup ("mutter");
	g_array_append_val (_tmp11_, _tmp12_);
	_tmp13_ = wm_list;
	_tmp14_ = g_strdup ("fluxbox");
	g_array_append_val (_tmp13_, _tmp14_);
	_tmp15_ = wm_list;
	_tmp16_ = g_strdup ("metacity");
	g_array_append_val (_tmp15_, _tmp16_);
	_tmp17_ = wm_list;
	_tmp18_ = g_strdup ("xfwin");
	g_array_append_val (_tmp17_, _tmp18_);
	_tmp19_ = wm_list;
	_tmp20_ = g_strdup ("matchbox");
	g_array_append_val (_tmp19_, _tmp20_);
	_tmp21_ = g_strdup ("");
	return_value = _tmp21_;
	{
		gint i = 0;
		i = 0;
		{
			gboolean _tmp22_ = FALSE;
			_tmp22_ = TRUE;
			while (TRUE) {
				GArray* _tmp24_;
				const gchar* wm = NULL;
				GArray* _tmp25_;
				const gchar* _tmp26_;
				gchar* test_wm = NULL;
				const gchar* _tmp27_;
				gchar* _tmp28_;
				const gchar* _tmp29_;
				if (!_tmp22_) {
					gint _tmp23_;
					i = i + 1;
					_tmp23_ = i;
				}
				_tmp22_ = FALSE;
				_tmp24_ = wm_list;
				if (!(((guint) i) < _tmp24_->len)) {
					break;
				}
				_tmp25_ = wm_list;
				_tmp26_ = g_array_index (_tmp25_, gchar*, (guint) i);
				wm = _tmp26_;
				_tmp27_ = wm;
				_tmp28_ = g_find_program_in_path (_tmp27_);
				test_wm = _tmp28_;
				_tmp29_ = test_wm;
				if (_tmp29_ != NULL) {
					const gchar* _tmp30_;
					const gchar* _tmp31_;
					gchar* _tmp32_;
					_tmp30_ = wm;
					g_message ("app.vala:332: Finding %s", _tmp30_);
					_tmp31_ = wm;
					_tmp32_ = g_strdup (_tmp31_);
					_g_free0 (return_value);
					return_value = _tmp32_;
					_g_free0 (test_wm);
					break;
				}
				_g_free0 (test_wm);
			}
		}
	}
	result = return_value;
	_g_array_unref0 (wm_list);
	return result;
}

static void
lxsession_windows_manager_app_fallback_window_manager (LxsessionWindowsManagerApp* self)
{
	/* Mirror of WindowsManagerApp.fallback_window_manager in app.vala. */
	gchar* _tmp0_;
	gchar** _tmp1_;
	gchar** _tmp2_;
	gint _tmp2__length1;
	LxsessionLxsessionConfigKeyFile* _tmp3_;
	g_return_if_fail (self != NULL);
	lxsession_app_object_set_name ((LxsessionAppObject*) self, "wm_safe");
	_tmp0_ = lxsession_windows_manager_app_find_window_manager (self);
	_tmp1_ = g_new0 (gchar*, 1 + 1);
	_tmp1_[0] = _tmp0_;
	_tmp2_ = _tmp1_;
	_tmp2__length1 = 1;
	lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp2_, 1);
	_tmp2_ = (_vala_array_free (_tmp2_, _tmp2__length1, (GDestroyNotify) g_free), NULL);
	_tmp3_ = lxsession_global_settings;
	lxsession_lxsession_config_set_generic_default ((LxsessionLxsessionConfig*) _tmp3_, "Session", "windows_manager", "command", "string", "wm_safe");
}

static void
lxsession_windows_manager_app_callback_pid (LxsessionWindowsManagerApp* self,
                                            GPid pid,
                                            gint status)
{
	const gchar* _tmp0_;
	const gchar* _tmp1_;
	gboolean _tmp2_;
	gboolean _tmp3_;
	g_return_if_fail (self != NULL);
	_tmp0_ = lxsession_app_object_get_name ((LxsessionAppObject*) self);
	_tmp1_ = _tmp0_;
	g_message ("app.vala:347: %s exit with this type of exit: %i\n", _tmp1_, status);
	gboolean was_reload = ((LxsessionAppObject*) self)->reload_pending;
	gboolean was_intentional = ((LxsessionAppObject*) self)->intentional_stop;
	g_spawn_close_pid (pid);
	if (((LxsessionAppObject*) self)->pid == pid) {
		((LxsessionAppObject*) self)->pid = 0;
		((LxsessionAppObject*) self)->reload_pending = FALSE;
		((LxsessionAppObject*) self)->intentional_stop = FALSE;
	}
	if (was_reload) {
		lxsession_windows_manager_app_launch (self);
		return;
	}
	/* No ChildWatch status is ever -1 (a failed spawn raises SpawnError in
	 * launch()); an intentional stop must not trigger the crash fallback. */
	if (WIFSIGNALED (status) && !was_intentional) {
		const gchar* _tmp_sig0_;
		_tmp_sig0_ = lxsession_app_object_get_name ((LxsessionAppObject*) self);
		g_warning ("app.vala:360: %s killed by signal %d, falling back to a safe window manager", _tmp_sig0_, WTERMSIG (status));
		lxsession_windows_manager_app_fallback_window_manager (self);
	}
	_tmp2_ = lxsession_app_object_get_guard ((LxsessionAppObject*) self);
	_tmp3_ = _tmp2_;
	if (_tmp3_ == TRUE) {
		/* waitpid-style status: decode with the W* macros (mirrors app.vala). */
		gint exit_code = -1;
		gint death_signal = -1;
		if (WIFEXITED (status)) {
			exit_code = WEXITSTATUS (status);
		} else if (WIFSIGNALED (status)) {
			death_signal = WTERMSIG (status);
		}
		if (exit_code == 0) {
			g_message ("app.vala:363: Exit normal, don't reload");
		} else if (death_signal == 15) {
			g_message ("app.vala:366: Exit by the user, don't reload");
		} else {
			gint _tmp8_;
			gint _tmp9_;
			gint _tmp10_;
			gint _tmp11_;
			gint _tmp12_;
			gint _tmp13_;
			g_message ("app.vala:372: Exit not normal, try to reload");
			if (((LxsessionAppObject*) self)->launch_time_us > 0 &&
			    g_get_monotonic_time () - ((LxsessionAppObject*) self)->launch_time_us >=
				    LUXESESSION_HEALTHY_RUNTIME_USEC)
				lxsession_app_object_set_crash_count ((LxsessionAppObject*) self, 0);
			_tmp8_ = lxsession_app_object_get_crash_count ((LxsessionAppObject*) self);
			_tmp9_ = _tmp8_;
			lxsession_app_object_set_crash_count ((LxsessionAppObject*) self, _tmp9_ + 1);
			_tmp10_ = lxsession_app_object_get_crash_count ((LxsessionAppObject*) self);
			_tmp11_ = _tmp10_;
			_tmp12_ = lxsession_app_object_get_stop_reload ((LxsessionAppObject*) self);
			_tmp13_ = _tmp12_;
			if (_tmp11_ <= _tmp13_) {
					((LxsessionAppObject*) self)->restart_source_id =
						g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
							lxsession_restart_delay_seconds ((guint)_tmp11_),
							lxsession_app_object_restart_after_backoff,
							g_object_ref (self), g_object_unref);
			} else {
				g_message ("app.vala:380: Application crashed too much, stop reloading");
			}
		}
	}
}

static void
_lxsession_windows_manager_app_callback_pid_gchild_watch_func (GPid pid,
                                                               gint wait_status,
                                                               gpointer self)
{
	lxsession_windows_manager_app_callback_pid ((LxsessionWindowsManagerApp*) self, pid, wait_status);
}

void
lxsession_windows_manager_app_launch (LxsessionWindowsManagerApp* self)
{
	const gchar* _tmp0_;
	const gchar* _tmp1_;
	GError* _inner_error0_ = NULL;
	g_return_if_fail (self != NULL);
	lxsession_app_object_read_config_settings ((LxsessionAppObject*) self);
	lxsession_app_object_read_settings ((LxsessionAppObject*) self);
	_tmp0_ = lxsession_app_object_get_name ((LxsessionAppObject*) self);
	_tmp1_ = _tmp0_;
	if (_tmp1_ != NULL) {
		{
			gchar** spawn_env = NULL;
			gchar** _tmp2_;
			gchar** _tmp3_;
			gint spawn_env_length1;
			gint _spawn_env_size_;
			gchar** _tmp4_;
			gint _tmp4__length1;
			gint _tmp5_ = 0;
			gchar** _tmp6_;
			gint _tmp6__length1;
			gchar** _tmp7_;
			gint _tmp7__length1;
			GPid _tmp8_ = 0;
			FILE* _tmp19_;
			_tmp3_ = _tmp2_ = g_get_environ ();
			spawn_env = _tmp3_;
			spawn_env_length1 = _vala_array_length (_tmp2_);
			_spawn_env_size_ = spawn_env_length1;
			_tmp4_ = lxsession_app_object_get_command ((LxsessionAppObject*) self, &_tmp5_);
			_tmp4__length1 = _tmp5_;
			_tmp6_ = _tmp4_;
			_tmp6__length1 = _tmp4__length1;
			_tmp7_ = spawn_env;
			_tmp7__length1 = spawn_env_length1;
			g_spawn_async (NULL, _tmp6_, _tmp7_, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &_tmp8_, &_inner_error0_);
			((LxsessionAppObject*) self)->pid = _tmp8_;
			((LxsessionAppObject*) self)->launch_time_us = g_get_monotonic_time ();
			if (G_UNLIKELY (_inner_error0_ != NULL)) {
				spawn_env = (_vala_array_free (spawn_env, spawn_env_length1, (GDestroyNotify) g_free), NULL);
				if (_inner_error0_->domain == G_SPAWN_ERROR) {
					goto __catch0_g_spawn_error;
				}
				g_critical ("file %s: line %d: unexpected error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
				g_clear_error (&_inner_error0_);
				return;
			}
			g_child_watch_add_full (G_PRIORITY_DEFAULT_IDLE, ((LxsessionAppObject*) self)->pid, _lxsession_windows_manager_app_callback_pid_gchild_watch_func, g_object_ref (self), g_object_unref);
			{
				gint a = 0;
				a = 0;
				{
					gboolean _tmp9_ = FALSE;
					_tmp9_ = TRUE;
					while (TRUE) {
						gchar** _tmp11_;
						gint _tmp11__length1;
						gint _tmp12_ = 0;
						gchar** _tmp13_;
						gint _tmp13__length1;
						FILE* _tmp14_;
						gchar** _tmp15_;
						gint _tmp15__length1;
						gint _tmp16_ = 0;
						gchar** _tmp17_;
						gint _tmp17__length1;
						const gchar* _tmp18_;
						if (!_tmp9_) {
							gint _tmp10_;
							_tmp10_ = a;
							a = _tmp10_ + 1;
						}
						_tmp9_ = FALSE;
						_tmp11_ = lxsession_app_object_get_command ((LxsessionAppObject*) self, &_tmp12_);
						_tmp11__length1 = _tmp12_;
						_tmp13_ = _tmp11_;
						_tmp13__length1 = _tmp11__length1;
						if (!(a < _tmp13__length1)) {
							break;
						}
						_tmp14_ = stdout;
						_tmp15_ = lxsession_app_object_get_command ((LxsessionAppObject*) self, &_tmp16_);
						_tmp15__length1 = _tmp16_;
						_tmp17_ = _tmp15_;
						_tmp17__length1 = _tmp15__length1;
						_tmp18_ = _tmp17_[a];
						fprintf (_tmp14_, "%s ", _tmp18_);
					}
				}
			}
			_tmp19_ = stdout;
			fprintf (_tmp19_, "\n");
			spawn_env = (_vala_array_free (spawn_env, spawn_env_length1, (GDestroyNotify) g_free), NULL);
		}
		goto __finally0;
		__catch0_g_spawn_error:
		{
			GError* err = NULL;
			GError* _tmp20_;
			const gchar* _tmp21_;
			const gchar* _tmp22_;
			err = _inner_error0_;
			_inner_error0_ = NULL;
			_tmp20_ = err;
			_tmp21_ = _tmp20_->message;
			g_warning ("app.vala:415: %s", _tmp21_);
			_tmp22_ = lxsession_app_object_get_name ((LxsessionAppObject*) self);
			g_warning ("app.vala:416: Error when launching %s, falling back to a safe window manager", _tmp22_);
			_g_error_free0 (err);
			/* A failed spawn never reaches callback_pid: fall back here.
			 * Guarded against recursion (relaunch re-reads wm_safe). */
			_tmp22_ = lxsession_app_object_get_name ((LxsessionAppObject*) self);
			if (g_strcmp0 (_tmp22_, "wm_safe") != 0) {
				lxsession_windows_manager_app_fallback_window_manager (self);
				lxsession_windows_manager_app_launch (self);
			}
		}
		__finally0:
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
			g_clear_error (&_inner_error0_);
			return;
		}
	}
}

static void
lxsession_windows_manager_app_class_init (LxsessionWindowsManagerAppClass * klass,
                                          gpointer klass_data)
{
	lxsession_windows_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionWindowsManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_windows_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_windows_manager_app_finalize;
}

static void
lxsession_windows_manager_app_instance_init (LxsessionWindowsManagerApp * self,
                                             gpointer klass)
{
	self->priv = lxsession_windows_manager_app_get_instance_private (self);
}

static void
lxsession_windows_manager_app_finalize (GObject * obj)
{
	LxsessionWindowsManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_WINDOWS_MANAGER_APP, LxsessionWindowsManagerApp);
	_g_free0 (self->priv->wm_command);
	_g_free0 (self->priv->mode);
	_g_free0 (self->priv->session);
	_g_free0 (self->priv->extras);
	G_OBJECT_CLASS (lxsession_windows_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_windows_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionWindowsManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_windows_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionWindowsManagerApp), 0, (GInstanceInitFunc) lxsession_windows_manager_app_instance_init, NULL };
	GType lxsession_windows_manager_app_type_id;
	lxsession_windows_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionWindowsManagerApp", &g_define_type_info, 0);
	LxsessionWindowsManagerApp_private_offset = g_type_add_instance_private (lxsession_windows_manager_app_type_id, sizeof (LxsessionWindowsManagerAppPrivate));
	return lxsession_windows_manager_app_type_id;
}

GType
lxsession_windows_manager_app_get_type (void)
{
	static volatile gsize lxsession_windows_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_windows_manager_app_type_id__once)) {
		GType lxsession_windows_manager_app_type_id;
		lxsession_windows_manager_app_type_id = lxsession_windows_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_windows_manager_app_type_id__once, lxsession_windows_manager_app_type_id);
	}
	return lxsession_windows_manager_app_type_id__once;
}

LxsessionPanelApp*
lxsession_panel_app_construct (GType object_type)
{
	LxsessionPanelApp * self = NULL;
	self = (LxsessionPanelApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionPanelApp*
lxsession_panel_app_new (void)
{
	return lxsession_panel_app_construct (LXSESSION_TYPE_PANEL_APP);
}

static void
lxsession_panel_app_real_read_config_settings (LxsessionAppObject* base)
{
	LxsessionPanelApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	LxsessionLxsessionConfigKeyFile* _tmp2_;
	gchar* _tmp3_;
	self = (LxsessionPanelApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "panel", "command");
	_g_free0 (self->panel_command);
	self->panel_command = _tmp1_;
	_tmp2_ = lxsession_global_settings;
	_tmp3_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp2_, "Session", "panel", "session");
	_g_free0 (self->panel_session);
	self->panel_session = _tmp3_;
}

static void
lxsession_panel_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionPanelApp * self;
	const gchar* _tmp0_;
	const gchar* _tmp1_;
	GQuark _tmp3_ = 0U;
	static GQuark _tmp2_label0 = 0;
	static GQuark _tmp2_label1 = 0;
	self = (LxsessionPanelApp*) base;
	_tmp0_ = self->panel_command;
	_tmp1_ = _tmp0_;
	_tmp3_ = (NULL == _tmp1_) ? 0 : g_quark_from_string (_tmp1_);
	if (_tmp3_ == ((0 != _tmp2_label0) ? _tmp2_label0 : (_tmp2_label0 = g_quark_from_static_string ("luxepanel")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp4_;
				const gchar* _tmp5_;
				_tmp4_ = self->panel_command;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp4_);
				_tmp5_ = self->panel_session;
				if (_tmp5_ != NULL) {
					gchar* create_command = NULL;
					const gchar* _tmp6_;
					gchar* _tmp7_;
					const gchar* _tmp8_;
					gchar** _tmp9_;
					gchar** _tmp10_;
					gchar** _tmp11_;
					gint _tmp11__length1;
					_tmp6_ = self->panel_session;
					_tmp7_ = g_strconcat ("luxepanel --profile ", _tmp6_, NULL);
					create_command = _tmp7_;
					_tmp8_ = create_command;
					_tmp10_ = _tmp9_ = lxsession_parse_command (_tmp8_, " ", 0);
					_tmp11_ = _tmp10_;
					_tmp11__length1 = _vala_array_length (_tmp9_);
					lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp11_, _vala_array_length (_tmp9_));
					_tmp11_ = (_vala_array_free (_tmp11_, _tmp11__length1, (GDestroyNotify) g_free), NULL);
					_g_free0 (create_command);
				} else {
					const gchar* _tmp12_;
					gchar* _tmp13_;
					gchar** _tmp14_;
					gchar** _tmp15_;
					gint _tmp15__length1;
					_tmp12_ = self->panel_command;
					_tmp13_ = g_strdup (_tmp12_);
					_tmp14_ = g_new0 (gchar*, 1 + 1);
					_tmp14_[0] = _tmp13_;
					_tmp15_ = _tmp14_;
					_tmp15__length1 = 1;
					lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp15_, 1);
					_tmp15_ = (_vala_array_free (_tmp15_, _tmp15__length1, (GDestroyNotify) g_free), NULL);
				}
				break;
			}
		}
	} else if (_tmp3_ == ((0 != _tmp2_label1) ? _tmp2_label1 : (_tmp2_label1 = g_quark_from_static_string ("awn")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp16_;
				gchar* create_command = NULL;
				gchar* _tmp17_;
				const gchar* _tmp18_;
				gchar** _tmp19_;
				gchar** _tmp20_;
				gchar** _tmp21_;
				gint _tmp21__length1;
				_tmp16_ = self->panel_command;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp16_);
				_tmp17_ = g_strdup ("avant-window-navigator");
				create_command = _tmp17_;
				_tmp18_ = create_command;
				_tmp20_ = _tmp19_ = lxsession_parse_command (_tmp18_, " ", 0);
				_tmp21_ = _tmp20_;
				_tmp21__length1 = _vala_array_length (_tmp19_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp21_, _vala_array_length (_tmp19_));
				_tmp21_ = (_vala_array_free (_tmp21_, _tmp21__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp22_;
				gchar** _tmp23_;
				gchar** _tmp24_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp25_;
				gint _tmp25__length1;
				const gchar* _tmp26_;
				gchar** _tmp27_;
				gint _tmp27__length1;
				_tmp22_ = self->panel_command;
				_tmp24_ = _tmp23_ = lxsession_parse_command (_tmp22_, " ", 0);
				create_command = _tmp24_;
				create_command_length1 = _vala_array_length (_tmp23_);
				_create_command_size_ = create_command_length1;
				_tmp25_ = create_command;
				_tmp25__length1 = create_command_length1;
				_tmp26_ = _tmp25_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp26_);
				_tmp27_ = create_command;
				_tmp27__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp27_, _tmp27__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
	lxsession_app_object_set_guard ((LxsessionAppObject*) self, TRUE);
}

static void
lxsession_panel_app_class_init (LxsessionPanelAppClass * klass,
                                gpointer klass_data)
{
	lxsession_panel_app_parent_class = g_type_class_peek_parent (klass);
	((LxsessionAppObjectClass *) klass)->read_config_settings = (void (*) (LxsessionAppObject*)) lxsession_panel_app_real_read_config_settings;
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_panel_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_panel_app_finalize;
}

static void
lxsession_panel_app_instance_init (LxsessionPanelApp * self,
                                   gpointer klass)
{
}

static void
lxsession_panel_app_finalize (GObject * obj)
{
	LxsessionPanelApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_PANEL_APP, LxsessionPanelApp);
	_g_free0 (self->panel_command);
	_g_free0 (self->panel_session);
	G_OBJECT_CLASS (lxsession_panel_app_parent_class)->finalize (obj);
}

static GType
lxsession_panel_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionPanelAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_panel_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionPanelApp), 0, (GInstanceInitFunc) lxsession_panel_app_instance_init, NULL };
	GType lxsession_panel_app_type_id;
	lxsession_panel_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionPanelApp", &g_define_type_info, 0);
	return lxsession_panel_app_type_id;
}

GType
lxsession_panel_app_get_type (void)
{
	static volatile gsize lxsession_panel_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_panel_app_type_id__once)) {
		GType lxsession_panel_app_type_id;
		lxsession_panel_app_type_id = lxsession_panel_app_get_type_once ();
		g_once_init_leave (&lxsession_panel_app_type_id__once, lxsession_panel_app_type_id);
	}
	return lxsession_panel_app_type_id__once;
}

LxsessionDockApp*
lxsession_dock_app_construct (GType object_type)
{
	LxsessionDockApp * self = NULL;
	self = (LxsessionDockApp*) lxsession_panel_app_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionDockApp*
lxsession_dock_app_new (void)
{
	return lxsession_dock_app_construct (LXSESSION_TYPE_DOCK_APP);
}

static void
lxsession_dock_app_real_read_config_settings (LxsessionAppObject* base)
{
	LxsessionDockApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	LxsessionLxsessionConfigKeyFile* _tmp2_;
	gchar* _tmp3_;
	self = (LxsessionDockApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "dock", "command");
	_g_free0 (((LxsessionPanelApp*) self)->panel_command);
	((LxsessionPanelApp*) self)->panel_command = _tmp1_;
	_tmp2_ = lxsession_global_settings;
	_tmp3_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp2_, "Session", "dock", "session");
	_g_free0 (((LxsessionPanelApp*) self)->panel_session);
	((LxsessionPanelApp*) self)->panel_session = _tmp3_;
}

static void
lxsession_dock_app_class_init (LxsessionDockAppClass * klass,
                               gpointer klass_data)
{
	lxsession_dock_app_parent_class = g_type_class_peek_parent (klass);
	((LxsessionAppObjectClass *) klass)->read_config_settings = (void (*) (LxsessionAppObject*)) lxsession_dock_app_real_read_config_settings;
}

static void
lxsession_dock_app_instance_init (LxsessionDockApp * self,
                                  gpointer klass)
{
}

static GType
lxsession_dock_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionDockAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_dock_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionDockApp), 0, (GInstanceInitFunc) lxsession_dock_app_instance_init, NULL };
	GType lxsession_dock_app_type_id;
	lxsession_dock_app_type_id = g_type_register_static (LXSESSION_TYPE_PANEL_APP, "LxsessionDockApp", &g_define_type_info, 0);
	return lxsession_dock_app_type_id;
}

GType
lxsession_dock_app_get_type (void)
{
	static volatile gsize lxsession_dock_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_dock_app_type_id__once)) {
		GType lxsession_dock_app_type_id;
		lxsession_dock_app_type_id = lxsession_dock_app_get_type_once ();
		g_once_init_leave (&lxsession_dock_app_type_id__once, lxsession_dock_app_type_id);
	}
	return lxsession_dock_app_type_id__once;
}

static inline gpointer
lxsession_screensaver_app_get_instance_private (LxsessionScreensaverApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionScreensaverApp_private_offset);
}

LxsessionScreensaverApp*
lxsession_screensaver_app_construct (GType object_type)
{
	LxsessionScreensaverApp * self = NULL;
	self = (LxsessionScreensaverApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionScreensaverApp*
lxsession_screensaver_app_new (void)
{
	return lxsession_screensaver_app_construct (LXSESSION_TYPE_SCREENSAVER_APP);
}

static void
lxsession_screensaver_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionScreensaverApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	static GQuark _tmp4_label0 = 0;
	self = (LxsessionScreensaverApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "screensaver", "command");
	_g_free0 (self->priv->screensaver_command);
	self->priv->screensaver_command = _tmp1_;
	_tmp2_ = self->priv->screensaver_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	if (_tmp5_ == ((0 != _tmp4_label0) ? _tmp4_label0 : (_tmp4_label0 = g_quark_from_static_string ("xscreensaver")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp6_;
				gchar* create_command = NULL;
				gchar* _tmp7_;
				const gchar* _tmp8_;
				gchar** _tmp9_;
				gchar** _tmp10_;
				gchar** _tmp11_;
				gint _tmp11__length1;
				_tmp6_ = self->priv->screensaver_command;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp6_);
				_tmp7_ = g_strdup ("xscreensaver -no-splash");
				create_command = _tmp7_;
				_tmp8_ = create_command;
				_tmp10_ = _tmp9_ = lxsession_parse_command (_tmp8_, " ", 0);
				_tmp11_ = _tmp10_;
				_tmp11__length1 = _vala_array_length (_tmp9_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp11_, _vala_array_length (_tmp9_));
				_tmp11_ = (_vala_array_free (_tmp11_, _tmp11__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp12_;
				gchar** _tmp13_;
				gchar** _tmp14_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp15_;
				gint _tmp15__length1;
				const gchar* _tmp16_;
				gchar** _tmp17_;
				gint _tmp17__length1;
				_tmp12_ = self->priv->screensaver_command;
				_tmp14_ = _tmp13_ = lxsession_parse_command (_tmp12_, " ", 0);
				create_command = _tmp14_;
				create_command_length1 = _vala_array_length (_tmp13_);
				_create_command_size_ = create_command_length1;
				_tmp15_ = create_command;
				_tmp15__length1 = create_command_length1;
				_tmp16_ = _tmp15_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp16_);
				_tmp17_ = create_command;
				_tmp17__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp17_, _tmp17__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
	lxsession_app_object_set_guard ((LxsessionAppObject*) self, TRUE);
}

static void
lxsession_screensaver_app_class_init (LxsessionScreensaverAppClass * klass,
                                      gpointer klass_data)
{
	lxsession_screensaver_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionScreensaverApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_screensaver_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_screensaver_app_finalize;
}

static void
lxsession_screensaver_app_instance_init (LxsessionScreensaverApp * self,
                                         gpointer klass)
{
	self->priv = lxsession_screensaver_app_get_instance_private (self);
}

static void
lxsession_screensaver_app_finalize (GObject * obj)
{
	LxsessionScreensaverApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_SCREENSAVER_APP, LxsessionScreensaverApp);
	_g_free0 (self->priv->screensaver_command);
	G_OBJECT_CLASS (lxsession_screensaver_app_parent_class)->finalize (obj);
}

static GType
lxsession_screensaver_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionScreensaverAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_screensaver_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionScreensaverApp), 0, (GInstanceInitFunc) lxsession_screensaver_app_instance_init, NULL };
	GType lxsession_screensaver_app_type_id;
	lxsession_screensaver_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionScreensaverApp", &g_define_type_info, 0);
	LxsessionScreensaverApp_private_offset = g_type_add_instance_private (lxsession_screensaver_app_type_id, sizeof (LxsessionScreensaverAppPrivate));
	return lxsession_screensaver_app_type_id;
}

GType
lxsession_screensaver_app_get_type (void)
{
	static volatile gsize lxsession_screensaver_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_screensaver_app_type_id__once)) {
		GType lxsession_screensaver_app_type_id;
		lxsession_screensaver_app_type_id = lxsession_screensaver_app_get_type_once ();
		g_once_init_leave (&lxsession_screensaver_app_type_id__once, lxsession_screensaver_app_type_id);
	}
	return lxsession_screensaver_app_type_id__once;
}

static inline gpointer
lxsession_power_manager_app_get_instance_private (LxsessionPowerManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionPowerManagerApp_private_offset);
}

LxsessionPowerManagerApp*
lxsession_power_manager_app_construct (GType object_type)
{
	LxsessionPowerManagerApp * self = NULL;
	self = (LxsessionPowerManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionPowerManagerApp*
lxsession_power_manager_app_new (void)
{
	return lxsession_power_manager_app_construct (LXSESSION_TYPE_POWER_MANAGER_APP);
}

static void
lxsession_power_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionPowerManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	LxsessionLxsessionConfigKeyFile* _tmp2_;
	gchar* _tmp3_;
	const gchar* _tmp4_;
	const gchar* _tmp5_;
	GQuark _tmp7_ = 0U;
	static GQuark _tmp6_label0 = 0;
	static GQuark _tmp6_label1 = 0;
	self = (LxsessionPowerManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "power_manager", "command");
	_g_free0 (self->priv->powermanager_command);
	self->priv->powermanager_command = _tmp1_;
	_tmp2_ = lxsession_global_settings;
	_tmp3_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp2_, "State", "laptop_mode", NULL);
	_g_free0 (self->priv->laptop_mode);
	self->priv->laptop_mode = _tmp3_;
	_tmp4_ = self->priv->powermanager_command;
	_tmp5_ = _tmp4_;
	_tmp7_ = (NULL == _tmp5_) ? 0 : g_quark_from_string (_tmp5_);
	if (_tmp7_ == ((0 != _tmp6_label0) ? _tmp6_label0 : (_tmp6_label0 = g_quark_from_static_string ("auto")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp8_;
				_tmp8_ = self->priv->laptop_mode;
				if (g_strcmp0 (_tmp8_, "yes") == 0) {
					gchar* create_command = NULL;
					gchar* _tmp9_;
					const gchar* _tmp10_;
					gchar** _tmp11_;
					gchar** _tmp12_;
					gchar** _tmp13_;
					gint _tmp13__length1;
					_tmp9_ = g_strdup ("xfce4-power-manager");
					create_command = _tmp9_;
					lxsession_app_object_set_name ((LxsessionAppObject*) self, "xfce4-power-manager");
					_tmp10_ = create_command;
					_tmp12_ = _tmp11_ = lxsession_parse_command (_tmp10_, " ", 0);
					_tmp13_ = _tmp12_;
					_tmp13__length1 = _vala_array_length (_tmp11_);
					lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp13_, _vala_array_length (_tmp11_));
					_tmp13_ = (_vala_array_free (_tmp13_, _tmp13__length1, (GDestroyNotify) g_free), NULL);
					_g_free0 (create_command);
				}
				break;
			}
		}
	} else if (_tmp7_ == ((0 != _tmp6_label1) ? _tmp6_label1 : (_tmp6_label1 = g_quark_from_static_string ("no")))) {
		switch (0) {
			default:
			{
				lxsession_app_object_set_name ((LxsessionAppObject*) self, "power_manager_off");
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp14_;
				gchar** _tmp15_;
				gchar** _tmp16_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp17_;
				gint _tmp17__length1;
				const gchar* _tmp18_;
				gchar** _tmp19_;
				gint _tmp19__length1;
				_tmp14_ = self->priv->powermanager_command;
				_tmp16_ = _tmp15_ = lxsession_parse_command (_tmp14_, " ", 0);
				create_command = _tmp16_;
				create_command_length1 = _vala_array_length (_tmp15_);
				_create_command_size_ = create_command_length1;
				_tmp17_ = create_command;
				_tmp17__length1 = create_command_length1;
				_tmp18_ = _tmp17_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp18_);
				_tmp19_ = create_command;
				_tmp19__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp19_, _tmp19__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
	lxsession_app_object_set_guard ((LxsessionAppObject*) self, TRUE);
}

static void
lxsession_power_manager_app_class_init (LxsessionPowerManagerAppClass * klass,
                                        gpointer klass_data)
{
	lxsession_power_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionPowerManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_power_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_power_manager_app_finalize;
}

static void
lxsession_power_manager_app_instance_init (LxsessionPowerManagerApp * self,
                                           gpointer klass)
{
	self->priv = lxsession_power_manager_app_get_instance_private (self);
}

static void
lxsession_power_manager_app_finalize (GObject * obj)
{
	LxsessionPowerManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_POWER_MANAGER_APP, LxsessionPowerManagerApp);
	_g_free0 (self->priv->powermanager_command);
	_g_free0 (self->priv->laptop_mode);
	G_OBJECT_CLASS (lxsession_power_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_power_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionPowerManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_power_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionPowerManagerApp), 0, (GInstanceInitFunc) lxsession_power_manager_app_instance_init, NULL };
	GType lxsession_power_manager_app_type_id;
	lxsession_power_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionPowerManagerApp", &g_define_type_info, 0);
	LxsessionPowerManagerApp_private_offset = g_type_add_instance_private (lxsession_power_manager_app_type_id, sizeof (LxsessionPowerManagerAppPrivate));
	return lxsession_power_manager_app_type_id;
}

GType
lxsession_power_manager_app_get_type (void)
{
	static volatile gsize lxsession_power_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_power_manager_app_type_id__once)) {
		GType lxsession_power_manager_app_type_id;
		lxsession_power_manager_app_type_id = lxsession_power_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_power_manager_app_type_id__once, lxsession_power_manager_app_type_id);
	}
	return lxsession_power_manager_app_type_id__once;
}

static inline gpointer
lxsession_file_manager_app_get_instance_private (LxsessionFileManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionFileManagerApp_private_offset);
}

LxsessionFileManagerApp*
lxsession_file_manager_app_construct (GType object_type)
{
	LxsessionFileManagerApp * self = NULL;
	self = (LxsessionFileManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionFileManagerApp*
lxsession_file_manager_app_new (void)
{
	return lxsession_file_manager_app_construct (LXSESSION_TYPE_FILE_MANAGER_APP);
}

static void
lxsession_file_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionFileManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	LxsessionLxsessionConfigKeyFile* _tmp2_;
	gchar* _tmp3_;
	LxsessionLxsessionConfigKeyFile* _tmp4_;
	gchar* _tmp5_;
	const gchar* _tmp6_;
	const gchar* _tmp7_;
	GQuark _tmp9_ = 0U;
	static GQuark _tmp8_label0 = 0;
	static GQuark _tmp8_label1 = 0;
	static GQuark _tmp8_label2 = 0;
	self = (LxsessionFileManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "file_manager", "command");
	_g_free0 (self->priv->filemanager_command);
	self->priv->filemanager_command = _tmp1_;
	_tmp2_ = lxsession_global_settings;
	_tmp3_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp2_, "Session", "file_manager", "session");
	_g_free0 (self->priv->filemanager_session);
	self->priv->filemanager_session = _tmp3_;
	_tmp4_ = lxsession_global_settings;
	_tmp5_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp4_, "Session", "file_manager", "extras");
	_g_free0 (self->priv->filemanager_extras);
	self->priv->filemanager_extras = _tmp5_;
	_tmp6_ = self->priv->filemanager_command;
	_tmp7_ = _tmp6_;
	_tmp9_ = (NULL == _tmp7_) ? 0 : g_quark_from_string (_tmp7_);
	if (_tmp9_ == ((0 != _tmp8_label0) ? _tmp8_label0 : (_tmp8_label0 = g_quark_from_static_string ("pcmanfm")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp10_;
				const gchar* _tmp11_;
				_tmp10_ = self->priv->filemanager_command;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp10_);
				_tmp11_ = self->priv->filemanager_session;
				if (_tmp11_ != NULL) {
					gchar* create_command = NULL;
					const gchar* _tmp12_;
					gchar* _tmp13_;
					gchar* _tmp14_;
					const gchar* _tmp15_;
					gchar* _tmp16_;
					gchar* _tmp17_;
					const gchar* _tmp18_;
					gchar** _tmp19_;
					gchar** _tmp20_;
					gchar** _tmp21_;
					gint _tmp21__length1;
					_tmp12_ = self->priv->filemanager_session;
					_tmp13_ = g_strconcat ("pcmanfm --profile ", _tmp12_, NULL);
					_tmp14_ = _tmp13_;
					_tmp15_ = self->priv->filemanager_extras;
					_tmp16_ = g_strconcat (_tmp14_, _tmp15_, NULL);
					_tmp17_ = _tmp16_;
					_g_free0 (_tmp14_);
					create_command = _tmp17_;
					_tmp18_ = create_command;
					_tmp20_ = _tmp19_ = lxsession_parse_command (_tmp18_, " ", 0);
					_tmp21_ = _tmp20_;
					_tmp21__length1 = _vala_array_length (_tmp19_);
					lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp21_, _vala_array_length (_tmp19_));
					_tmp21_ = (_vala_array_free (_tmp21_, _tmp21__length1, (GDestroyNotify) g_free), NULL);
					_g_free0 (create_command);
				} else {
					const gchar* _tmp22_;
					gchar* _tmp23_;
					gchar** _tmp24_;
					gchar** _tmp25_;
					gint _tmp25__length1;
					_tmp22_ = self->priv->filemanager_command;
					_tmp23_ = g_strdup (_tmp22_);
					_tmp24_ = g_new0 (gchar*, 1 + 1);
					_tmp24_[0] = _tmp23_;
					_tmp25_ = _tmp24_;
					_tmp25__length1 = 1;
					lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp25_, 1);
					_tmp25_ = (_vala_array_free (_tmp25_, _tmp25__length1, (GDestroyNotify) g_free), NULL);
				}
				break;
			}
		}
	} else if (_tmp9_ == ((0 != _tmp8_label1) ? _tmp8_label1 : (_tmp8_label1 = g_quark_from_static_string ("pcmanfm-qt")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp26_;
				const gchar* _tmp27_;
				_tmp26_ = self->priv->filemanager_command;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp26_);
				_tmp27_ = self->priv->filemanager_session;
				if (_tmp27_ != NULL) {
					gchar* create_command = NULL;
					const gchar* _tmp28_;
					gchar* _tmp29_;
					gchar* _tmp30_;
					const gchar* _tmp31_;
					gchar* _tmp32_;
					gchar* _tmp33_;
					const gchar* _tmp34_;
					gchar** _tmp35_;
					gchar** _tmp36_;
					gchar** _tmp37_;
					gint _tmp37__length1;
					_tmp28_ = self->priv->filemanager_session;
					_tmp29_ = g_strconcat ("pcmanfm-qt --profile ", _tmp28_, NULL);
					_tmp30_ = _tmp29_;
					_tmp31_ = self->priv->filemanager_extras;
					_tmp32_ = g_strconcat (_tmp30_, _tmp31_, NULL);
					_tmp33_ = _tmp32_;
					_g_free0 (_tmp30_);
					create_command = _tmp33_;
					_tmp34_ = create_command;
					_tmp36_ = _tmp35_ = lxsession_parse_command (_tmp34_, " ", 0);
					_tmp37_ = _tmp36_;
					_tmp37__length1 = _vala_array_length (_tmp35_);
					lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp37_, _vala_array_length (_tmp35_));
					_tmp37_ = (_vala_array_free (_tmp37_, _tmp37__length1, (GDestroyNotify) g_free), NULL);
					_g_free0 (create_command);
				} else {
					const gchar* _tmp38_;
					gchar* _tmp39_;
					gchar** _tmp40_;
					gchar** _tmp41_;
					gint _tmp41__length1;
					_tmp38_ = self->priv->filemanager_command;
					_tmp39_ = g_strdup (_tmp38_);
					_tmp40_ = g_new0 (gchar*, 1 + 1);
					_tmp40_[0] = _tmp39_;
					_tmp41_ = _tmp40_;
					_tmp41__length1 = 1;
					lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp41_, 1);
					_tmp41_ = (_vala_array_free (_tmp41_, _tmp41__length1, (GDestroyNotify) g_free), NULL);
				}
				break;
			}
		}
	} else if (_tmp9_ == ((0 != _tmp8_label2) ? _tmp8_label2 : (_tmp8_label2 = g_quark_from_static_string ("nautilus")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp42_;
				gchar* create_command = NULL;
				const gchar* _tmp43_;
				gchar* _tmp44_;
				const gchar* _tmp45_;
				gchar** _tmp46_;
				gchar** _tmp47_;
				gchar** _tmp48_;
				gint _tmp48__length1;
				_tmp42_ = self->priv->filemanager_command;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp42_);
				_tmp43_ = self->priv->filemanager_extras;
				_tmp44_ = g_strconcat ("nautilus" " -n ", _tmp43_, NULL);
				create_command = _tmp44_;
				_tmp45_ = create_command;
				_tmp47_ = _tmp46_ = lxsession_parse_command (_tmp45_, " ", 0);
				_tmp48_ = _tmp47_;
				_tmp48__length1 = _vala_array_length (_tmp46_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp48_, _vala_array_length (_tmp46_));
				_tmp48_ = (_vala_array_free (_tmp48_, _tmp48__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp49_;
				gchar** _tmp50_;
				gchar** _tmp51_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp52_;
				gint _tmp52__length1;
				const gchar* _tmp53_;
				gchar** _tmp54_;
				gint _tmp54__length1;
				_tmp49_ = self->priv->filemanager_command;
				_tmp51_ = _tmp50_ = lxsession_parse_command (_tmp49_, " ", 0);
				create_command = _tmp51_;
				create_command_length1 = _vala_array_length (_tmp50_);
				_create_command_size_ = create_command_length1;
				_tmp52_ = create_command;
				_tmp52__length1 = create_command_length1;
				_tmp53_ = _tmp52_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp53_);
				_tmp54_ = create_command;
				_tmp54__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp54_, _tmp54__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

static void
lxsession_file_manager_app_class_init (LxsessionFileManagerAppClass * klass,
                                       gpointer klass_data)
{
	lxsession_file_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionFileManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_file_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_file_manager_app_finalize;
}

static void
lxsession_file_manager_app_instance_init (LxsessionFileManagerApp * self,
                                          gpointer klass)
{
	self->priv = lxsession_file_manager_app_get_instance_private (self);
}

static void
lxsession_file_manager_app_finalize (GObject * obj)
{
	LxsessionFileManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_FILE_MANAGER_APP, LxsessionFileManagerApp);
	_g_free0 (self->priv->filemanager_command);
	_g_free0 (self->priv->filemanager_session);
	_g_free0 (self->priv->filemanager_extras);
	G_OBJECT_CLASS (lxsession_file_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_file_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionFileManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_file_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionFileManagerApp), 0, (GInstanceInitFunc) lxsession_file_manager_app_instance_init, NULL };
	GType lxsession_file_manager_app_type_id;
	lxsession_file_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionFileManagerApp", &g_define_type_info, 0);
	LxsessionFileManagerApp_private_offset = g_type_add_instance_private (lxsession_file_manager_app_type_id, sizeof (LxsessionFileManagerAppPrivate));
	return lxsession_file_manager_app_type_id;
}

GType
lxsession_file_manager_app_get_type (void)
{
	static volatile gsize lxsession_file_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_file_manager_app_type_id__once)) {
		GType lxsession_file_manager_app_type_id;
		lxsession_file_manager_app_type_id = lxsession_file_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_file_manager_app_type_id__once, lxsession_file_manager_app_type_id);
	}
	return lxsession_file_manager_app_type_id__once;
}

static inline gpointer
lxsession_desktop_app_get_instance_private (LxsessionDesktopApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionDesktopApp_private_offset);
}

LxsessionDesktopApp*
lxsession_desktop_app_construct (GType object_type)
{
	LxsessionDesktopApp * self = NULL;
	self = (LxsessionDesktopApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionDesktopApp*
lxsession_desktop_app_new (void)
{
	return lxsession_desktop_app_construct (LXSESSION_TYPE_DESKTOP_APP);
}

static gpointer
_g_object_ref0 (gpointer self)
{
	return self ? g_object_ref (self) : NULL;
}

static void
lxsession_desktop_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionDesktopApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	LxsessionLxsessionConfigKeyFile* _tmp2_;
	gchar* _tmp3_;
	const gchar* _tmp4_;
	const gchar* _tmp5_;
	GQuark _tmp7_ = 0U;
	static GQuark _tmp6_label0 = 0;
	static GQuark _tmp6_label1 = 0;
	self = (LxsessionDesktopApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "desktop_manager", "command");
	_g_free0 (self->priv->desktop_command);
	self->priv->desktop_command = _tmp1_;
	_tmp2_ = lxsession_global_settings;
	_tmp3_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp2_, "Session", "desktop_manager", "wallpaper");
	_g_free0 (self->priv->desktop_wallpaper);
	self->priv->desktop_wallpaper = _tmp3_;
	_tmp4_ = self->priv->desktop_command;
	_tmp5_ = _tmp4_;
	_tmp7_ = (NULL == _tmp5_) ? 0 : g_quark_from_string (_tmp5_);
	if (_tmp7_ == ((0 != _tmp6_label0) ? _tmp6_label0 : (_tmp6_label0 = g_quark_from_static_string ("filemanager")))) {
		switch (0) {
			default:
			{
				gchar* filemanager_session = NULL;
				LxsessionLxsessionConfigKeyFile* _tmp8_;
				gchar* _tmp9_;
				gchar* filemanager_extras = NULL;
				LxsessionLxsessionConfigKeyFile* _tmp10_;
				gchar* _tmp11_;
				LxsessionFileManagerApp* _tmp12_;
				LxsessionLxsessionConfigKeyFile* _tmp16_;
				gchar* _tmp17_;
				gchar* _tmp18_;
				GQuark _tmp20_ = 0U;
				static GQuark _tmp19_label0 = 0;
				static GQuark _tmp19_label1 = 0;
				static GQuark _tmp19_label2 = 0;
				_tmp8_ = lxsession_global_settings;
				_tmp9_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp8_, "Session", "file_manager", "session");
				filemanager_session = _tmp9_;
				_tmp10_ = lxsession_global_settings;
				_tmp11_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp10_, "Session", "file_manager", "extras");
				filemanager_extras = _tmp11_;
				_tmp12_ = lxsession_global_file_manager;
				if (_tmp12_ != NULL) {
					LxsessionFileManagerApp* filemanager = NULL;
					LxsessionFileManagerApp* _tmp13_;
					LxsessionFileManagerApp* _tmp14_;
					LxsessionFileManagerApp* _tmp15_;
					g_message ("app.vala:637: File manager needed for desktop manager but doesn't exis" \
"t, creating it");
					_tmp13_ = lxsession_file_manager_app_new ();
					filemanager = _tmp13_;
					_tmp14_ = filemanager;
					_tmp15_ = _g_object_ref0 (_tmp14_);
					_g_object_unref0 (lxsession_global_file_manager);
					lxsession_global_file_manager = _tmp15_;
					_g_object_unref0 (filemanager);
				}
				_tmp16_ = lxsession_global_settings;
				_tmp17_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp16_, "Session", "file_manager", "command");
				_tmp18_ = _tmp17_;
				_tmp20_ = (NULL == _tmp18_) ? 0 : g_quark_from_string (_tmp18_);
				g_free (_tmp18_);
				if (_tmp20_ == ((0 != _tmp19_label0) ? _tmp19_label0 : (_tmp19_label0 = g_quark_from_static_string ("pcmanfm")))) {
					switch (0) {
						default:
						{
							LxsessionLxsessionConfigKeyFile* _tmp21_;
							gchar* _tmp22_;
							gchar* _tmp23_;
							gchar* create_command = NULL;
							const gchar* _tmp24_;
							gchar* _tmp25_;
							gchar* _tmp26_;
							const gchar* _tmp27_;
							gchar* _tmp28_;
							gchar* _tmp29_;
							const gchar* _tmp30_;
							gchar** _tmp31_;
							gchar** _tmp32_;
							gchar** _tmp33_;
							gint _tmp33__length1;
							_tmp21_ = lxsession_global_settings;
							_tmp22_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp21_, "Session", "file_manager", "command");
							_tmp23_ = _tmp22_;
							lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp23_);
							_g_free0 (_tmp23_);
							_tmp24_ = filemanager_session;
							_tmp25_ = g_strconcat ("pcmanfm --desktop --profile ", _tmp24_, NULL);
							_tmp26_ = _tmp25_;
							_tmp27_ = filemanager_extras;
							_tmp28_ = g_strconcat (_tmp26_, _tmp27_, NULL);
							_tmp29_ = _tmp28_;
							_g_free0 (_tmp26_);
							create_command = _tmp29_;
							_tmp30_ = create_command;
							_tmp32_ = _tmp31_ = lxsession_parse_command (_tmp30_, " ", 0);
							_tmp33_ = _tmp32_;
							_tmp33__length1 = _vala_array_length (_tmp31_);
							lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp33_, _vala_array_length (_tmp31_));
							_tmp33_ = (_vala_array_free (_tmp33_, _tmp33__length1, (GDestroyNotify) g_free), NULL);
							_g_free0 (create_command);
							break;
						}
					}
				} else if (_tmp20_ == ((0 != _tmp19_label1) ? _tmp19_label1 : (_tmp19_label1 = g_quark_from_static_string ("pcmanfm-qt")))) {
					switch (0) {
						default:
						{
							LxsessionLxsessionConfigKeyFile* _tmp34_;
							gchar* _tmp35_;
							gchar* _tmp36_;
							gchar* create_command = NULL;
							const gchar* _tmp37_;
							gchar* _tmp38_;
							gchar* _tmp39_;
							const gchar* _tmp40_;
							gchar* _tmp41_;
							gchar* _tmp42_;
							const gchar* _tmp43_;
							gchar** _tmp44_;
							gchar** _tmp45_;
							gchar** _tmp46_;
							gint _tmp46__length1;
							_tmp34_ = lxsession_global_settings;
							_tmp35_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp34_, "Session", "file_manager", "command");
							_tmp36_ = _tmp35_;
							lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp36_);
							_g_free0 (_tmp36_);
							_tmp37_ = filemanager_session;
							_tmp38_ = g_strconcat ("pcmanfm-qt --desktop --profile ", _tmp37_, NULL);
							_tmp39_ = _tmp38_;
							_tmp40_ = filemanager_extras;
							_tmp41_ = g_strconcat (_tmp39_, _tmp40_, NULL);
							_tmp42_ = _tmp41_;
							_g_free0 (_tmp39_);
							create_command = _tmp42_;
							_tmp43_ = create_command;
							_tmp45_ = _tmp44_ = lxsession_parse_command (_tmp43_, " ", 0);
							_tmp46_ = _tmp45_;
							_tmp46__length1 = _vala_array_length (_tmp44_);
							lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp46_, _vala_array_length (_tmp44_));
							_tmp46_ = (_vala_array_free (_tmp46_, _tmp46__length1, (GDestroyNotify) g_free), NULL);
							_g_free0 (create_command);
							break;
						}
					}
				} else if (_tmp20_ == ((0 != _tmp19_label2) ? _tmp19_label2 : (_tmp19_label2 = g_quark_from_static_string ("nautilus")))) {
					switch (0) {
						default:
						{
							LxsessionLxsessionConfigKeyFile* _tmp47_;
							gchar* _tmp48_;
							gchar* _tmp49_;
							gchar* create_command = NULL;
							const gchar* _tmp50_;
							gchar* _tmp51_;
							const gchar* _tmp52_;
							gchar** _tmp53_;
							gchar** _tmp54_;
							gchar** _tmp55_;
							gint _tmp55__length1;
							_tmp47_ = lxsession_global_settings;
							_tmp48_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp47_, "Session", "file_manager", "command");
							_tmp49_ = _tmp48_;
							lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp49_);
							_g_free0 (_tmp49_);
							_tmp50_ = filemanager_extras;
							_tmp51_ = g_strconcat ("nautilus" " -n ", _tmp50_, NULL);
							create_command = _tmp51_;
							_tmp52_ = create_command;
							_tmp54_ = _tmp53_ = lxsession_parse_command (_tmp52_, " ", 0);
							_tmp55_ = _tmp54_;
							_tmp55__length1 = _vala_array_length (_tmp53_);
							lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp55_, _vala_array_length (_tmp53_));
							_tmp55_ = (_vala_array_free (_tmp55_, _tmp55__length1, (GDestroyNotify) g_free), NULL);
							_g_free0 (create_command);
							break;
						}
					}
				}
				_g_free0 (filemanager_extras);
				_g_free0 (filemanager_session);
				break;
			}
		}
	} else if (_tmp7_ == ((0 != _tmp6_label1) ? _tmp6_label1 : (_tmp6_label1 = g_quark_from_static_string ("feh")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp56_;
				gchar* create_command = NULL;
				const gchar* _tmp57_;
				gchar* _tmp58_;
				const gchar* _tmp59_;
				gchar** _tmp60_;
				gchar** _tmp61_;
				gchar** _tmp62_;
				gint _tmp62__length1;
				_tmp56_ = self->priv->desktop_command;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp56_);
				_tmp57_ = self->priv->desktop_wallpaper;
				_tmp58_ = g_strconcat ("feh" " --bg-scale ", _tmp57_, NULL);
				create_command = _tmp58_;
				_tmp59_ = create_command;
				_tmp61_ = _tmp60_ = lxsession_parse_command (_tmp59_, " ", 0);
				_tmp62_ = _tmp61_;
				_tmp62__length1 = _vala_array_length (_tmp60_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp62_, _vala_array_length (_tmp60_));
				_tmp62_ = (_vala_array_free (_tmp62_, _tmp62__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp63_;
				gchar** _tmp64_;
				gchar** _tmp65_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp66_;
				gint _tmp66__length1;
				const gchar* _tmp67_;
				gchar** _tmp68_;
				gint _tmp68__length1;
				_tmp63_ = self->priv->desktop_command;
				_tmp65_ = _tmp64_ = lxsession_parse_command (_tmp63_, " ", 0);
				create_command = _tmp65_;
				create_command_length1 = _vala_array_length (_tmp64_);
				_create_command_size_ = create_command_length1;
				_tmp66_ = create_command;
				_tmp66__length1 = create_command_length1;
				_tmp67_ = _tmp66_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp67_);
				_tmp68_ = create_command;
				_tmp68__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp68_, _tmp68__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
	lxsession_app_object_set_guard ((LxsessionAppObject*) self, TRUE);
}

static gchar**
_vala_array_dup2 (gchar** self,
                  gssize length)
{
	if (length >= 0) {
		gchar** result;
		gssize i;
		result = g_new0 (gchar*, length + 1);
		for (i = 0; i < length; i++) {
			gchar* _tmp0_;
			_tmp0_ = g_strdup (self[i]);
			result[i] = _tmp0_;
		}
		return result;
	}
	return NULL;
}

void
lxsession_desktop_app_launch_settings (LxsessionDesktopApp* self)
{
	gchar** backup_command = NULL;
	gchar** _tmp0_;
	gint _tmp0__length1;
	gint _tmp1_ = 0;
	gchar** _tmp2_;
	gint _tmp2__length1;
	gchar** _tmp3_;
	gint _tmp3__length1;
	gint backup_command_length1;
	gint _backup_command_size_;
	const gchar* _tmp4_;
	const gchar* _tmp5_;
	const gchar* _tmp6_;
	GQuark _tmp8_ = 0U;
	static GQuark _tmp7_label0 = 0;
	gchar** _tmp14_;
	gint _tmp14__length1;
	g_return_if_fail (self != NULL);
	_tmp0_ = lxsession_app_object_get_command ((LxsessionAppObject*) self, &_tmp1_);
	/* Frame owns wallpaper now; settings must not relaunch its daemon. */
	if (_tmp1_ > 0 && g_strcmp0 (_tmp0_[0], "frame") == 0) {
		gchar *settings_argv[] = { "frame", "--wallpaper-settings", NULL };
		GError *error = NULL;
		if (!g_spawn_async (NULL, settings_argv, NULL, G_SPAWN_SEARCH_PATH,
		                    NULL, NULL, NULL, &error)) {
			g_warning ("Cannot open wallpaper settings: %s", error->message);
			g_clear_error (&error);
		}
		return;
	}
	_tmp0__length1 = _tmp1_;
	_tmp2_ = _tmp0_;
	_tmp2__length1 = _tmp0__length1;
	_tmp3_ = (_tmp2_ != NULL) ? _vala_array_dup2 (_tmp2_, _tmp2__length1) : _tmp2_;
	_tmp3__length1 = _tmp2__length1;
	backup_command = _tmp3_;
	backup_command_length1 = _tmp3__length1;
	_backup_command_size_ = backup_command_length1;
	_tmp4_ = lxsession_app_object_get_name ((LxsessionAppObject*) self);
	_tmp5_ = _tmp4_;
	_tmp6_ = _tmp5_;
	_tmp8_ = (NULL == _tmp6_) ? 0 : g_quark_from_string (_tmp6_);
	if (_tmp8_ == ((0 != _tmp7_label0) ? _tmp7_label0 : (_tmp7_label0 = g_quark_from_static_string ("pcmanfm")))) {
		switch (0) {
			default:
			{
				gchar* create_settings_command = NULL;
				gchar* _tmp9_;
				const gchar* _tmp10_;
				gchar** _tmp11_;
				gchar** _tmp12_;
				gchar** _tmp13_;
				gint _tmp13__length1;
				_tmp9_ = g_strdup ("pcmanfm --desktop-pref");
				create_settings_command = _tmp9_;
				_tmp10_ = create_settings_command;
				_tmp12_ = _tmp11_ = lxsession_parse_command (_tmp10_, " ", 0);
				_tmp13_ = _tmp12_;
				_tmp13__length1 = _vala_array_length (_tmp11_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp13_, _vala_array_length (_tmp11_));
				_tmp13_ = (_vala_array_free (_tmp13_, _tmp13__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_settings_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				break;
			}
		}
	}
	lxsession_app_object_launch ((LxsessionAppObject*) self);
	_tmp14_ = backup_command;
	_tmp14__length1 = backup_command_length1;
	lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp14_, _tmp14__length1);
	backup_command = (_vala_array_free (backup_command, backup_command_length1, (GDestroyNotify) g_free), NULL);
}

static void
lxsession_desktop_app_class_init (LxsessionDesktopAppClass * klass,
                                  gpointer klass_data)
{
	lxsession_desktop_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionDesktopApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_desktop_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_desktop_app_finalize;
}

static void
lxsession_desktop_app_instance_init (LxsessionDesktopApp * self,
                                     gpointer klass)
{
	self->priv = lxsession_desktop_app_get_instance_private (self);
}

static void
lxsession_desktop_app_finalize (GObject * obj)
{
	LxsessionDesktopApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_DESKTOP_APP, LxsessionDesktopApp);
	_g_free0 (self->priv->desktop_command);
	_g_free0 (self->priv->desktop_wallpaper);
	G_OBJECT_CLASS (lxsession_desktop_app_parent_class)->finalize (obj);
}

static GType
lxsession_desktop_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionDesktopAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_desktop_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionDesktopApp), 0, (GInstanceInitFunc) lxsession_desktop_app_instance_init, NULL };
	GType lxsession_desktop_app_type_id;
	lxsession_desktop_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionDesktopApp", &g_define_type_info, 0);
	LxsessionDesktopApp_private_offset = g_type_add_instance_private (lxsession_desktop_app_type_id, sizeof (LxsessionDesktopAppPrivate));
	return lxsession_desktop_app_type_id;
}

GType
lxsession_desktop_app_get_type (void)
{
	static volatile gsize lxsession_desktop_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_desktop_app_type_id__once)) {
		GType lxsession_desktop_app_type_id;
		lxsession_desktop_app_type_id = lxsession_desktop_app_get_type_once ();
		g_once_init_leave (&lxsession_desktop_app_type_id__once, lxsession_desktop_app_type_id);
	}
	return lxsession_desktop_app_type_id__once;
}

static inline gpointer
lxsession_polkit_app_get_instance_private (LxsessionPolkitApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionPolkitApp_private_offset);
}

LxsessionPolkitApp*
lxsession_polkit_app_construct (GType object_type)
{
	LxsessionPolkitApp * self = NULL;
	self = (LxsessionPolkitApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionPolkitApp*
lxsession_polkit_app_new (void)
{
	return lxsession_polkit_app_construct (LXSESSION_TYPE_POLKIT_APP);
}

static void
lxsession_polkit_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionPolkitApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	static GQuark _tmp4_label0 = 0;
	static GQuark _tmp4_label1 = 0;
	static GQuark _tmp4_label2 = 0;
	self = (LxsessionPolkitApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "polkit", "command");
	_g_free0 (self->priv->polkit_command);
	self->priv->polkit_command = _tmp1_;
	_tmp2_ = self->priv->polkit_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	if (_tmp5_ == ((0 != _tmp4_label0) ? _tmp4_label0 : (_tmp4_label0 = g_quark_from_static_string ("gnome")))) {
		switch (0) {
			default:
			{
				gchar* create_command = NULL;
				gchar* _tmp6_;
				const gchar* _tmp7_;
				gchar** _tmp8_;
				gchar** _tmp9_;
				gchar** _tmp10_;
				gint _tmp10__length1;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, "polkit-gnome-authentication-agent-1");
				_tmp6_ = g_find_program_in_path ("polkit-gnome-authentication-agent-1");
				if (_tmp6_ == NULL)
					_tmp6_ = g_strdup ("polkit-gnome-authentication-agent-1");
				create_command = _tmp6_;
				_tmp7_ = create_command;
				_tmp9_ = _tmp8_ = lxsession_parse_command (_tmp7_, " ", 0);
				_tmp10_ = _tmp9_;
				_tmp10__length1 = _vala_array_length (_tmp8_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp10_, _vala_array_length (_tmp8_));
				_tmp10_ = (_vala_array_free (_tmp10_, _tmp10__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label1) ? _tmp4_label1 : (_tmp4_label1 = g_quark_from_static_string ("razorqt")))) {
		switch (0) {
			default:
			{
				gchar* create_command = NULL;
				gchar* _tmp11_;
				const gchar* _tmp12_;
				gchar** _tmp13_;
				gchar** _tmp14_;
				gchar** _tmp15_;
				gint _tmp15__length1;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, "razor-policykit-agent");
				_tmp11_ = g_find_program_in_path ("razor-policykit-agent");
				if (_tmp11_ == NULL)
					_tmp11_ = g_strdup ("razor-policykit-agent");
				create_command = _tmp11_;
				_tmp12_ = create_command;
				_tmp14_ = _tmp13_ = lxsession_parse_command (_tmp12_, " ", 0);
				_tmp15_ = _tmp14_;
				_tmp15__length1 = _vala_array_length (_tmp13_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp15_, _vala_array_length (_tmp13_));
				_tmp15_ = (_vala_array_free (_tmp15_, _tmp15__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label2) ? _tmp4_label2 : (_tmp4_label2 = g_quark_from_static_string ("luxepolkit")))) {
		switch (0) {
			default:
			{
				gchar* create_command = NULL;
				gchar* _tmp16_;
				const gchar* _tmp17_;
				gchar** _tmp18_;
				gchar** _tmp19_;
				gchar** _tmp20_;
				gint _tmp20__length1;
				g_message ("app.vala:721: polkit separate");
				lxsession_app_object_set_name ((LxsessionAppObject*) self, "luxepolkit");
				_tmp16_ = g_strdup ("luxepolkit");
				create_command = _tmp16_;
				_tmp17_ = create_command;
				_tmp19_ = _tmp18_ = lxsession_parse_command (_tmp17_, " ", 0);
				_tmp20_ = _tmp19_;
				_tmp20__length1 = _vala_array_length (_tmp18_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp20_, _vala_array_length (_tmp18_));
				_tmp20_ = (_vala_array_free (_tmp20_, _tmp20__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	}
	lxsession_app_object_set_guard ((LxsessionAppObject*) self, TRUE);
}

void
lxsession_polkit_app_deactivate (LxsessionPolkitApp* self)
{
	g_return_if_fail (self != NULL);
}

static void
lxsession_polkit_app_class_init (LxsessionPolkitAppClass * klass,
                                 gpointer klass_data)
{
	lxsession_polkit_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionPolkitApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_polkit_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_polkit_app_finalize;
}

static void
lxsession_polkit_app_instance_init (LxsessionPolkitApp * self,
                                    gpointer klass)
{
	self->priv = lxsession_polkit_app_get_instance_private (self);
}

static void
lxsession_polkit_app_finalize (GObject * obj)
{
	LxsessionPolkitApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_POLKIT_APP, LxsessionPolkitApp);
	_g_free0 (self->priv->polkit_command);
	G_OBJECT_CLASS (lxsession_polkit_app_parent_class)->finalize (obj);
}

static GType
lxsession_polkit_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionPolkitAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_polkit_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionPolkitApp), 0, (GInstanceInitFunc) lxsession_polkit_app_instance_init, NULL };
	GType lxsession_polkit_app_type_id;
	lxsession_polkit_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionPolkitApp", &g_define_type_info, 0);
	LxsessionPolkitApp_private_offset = g_type_add_instance_private (lxsession_polkit_app_type_id, sizeof (LxsessionPolkitAppPrivate));
	return lxsession_polkit_app_type_id;
}

GType
lxsession_polkit_app_get_type (void)
{
	static volatile gsize lxsession_polkit_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_polkit_app_type_id__once)) {
		GType lxsession_polkit_app_type_id;
		lxsession_polkit_app_type_id = lxsession_polkit_app_get_type_once ();
		g_once_init_leave (&lxsession_polkit_app_type_id__once, lxsession_polkit_app_type_id);
	}
	return lxsession_polkit_app_type_id__once;
}

static inline gpointer
lxsession_network_gui_app_get_instance_private (LxsessionNetworkGuiApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionNetworkGuiApp_private_offset);
}

LxsessionNetworkGuiApp*
lxsession_network_gui_app_construct (GType object_type)
{
	LxsessionNetworkGuiApp * self = NULL;
	self = (LxsessionNetworkGuiApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionNetworkGuiApp*
lxsession_network_gui_app_new (void)
{
	return lxsession_network_gui_app_construct (LXSESSION_TYPE_NETWORK_GUI_APP);
}

static void
lxsession_network_gui_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionNetworkGuiApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	LxsessionLxsessionConfigKeyFile* _tmp2_;
	gchar* _tmp3_;
	const gchar* _tmp4_;
	const gchar* _tmp5_;
	GQuark _tmp7_ = 0U;
	static GQuark _tmp6_label0 = 0;
	static GQuark _tmp6_label1 = 0;
	self = (LxsessionNetworkGuiApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "network_gui", "command");
	_g_free0 (self->priv->network_command);
	self->priv->network_command = _tmp1_;
	_tmp2_ = lxsession_global_settings;
	_tmp3_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp2_, "State", "laptop_mode", NULL);
	_g_free0 (self->priv->laptop_mode);
	self->priv->laptop_mode = _tmp3_;
	_tmp4_ = self->priv->network_command;
	_tmp5_ = _tmp4_;
	_tmp7_ = (NULL == _tmp5_) ? 0 : g_quark_from_string (_tmp5_);
	if (_tmp7_ == ((0 != _tmp6_label0) ? _tmp6_label0 : (_tmp6_label0 = g_quark_from_static_string ("no")))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp7_ == ((0 != _tmp6_label1) ? _tmp6_label1 : (_tmp6_label1 = g_quark_from_static_string ("auto")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp8_;
				_tmp8_ = self->priv->laptop_mode;
				if (g_strcmp0 (_tmp8_, "yes") == 0) {
					gchar* test_nm_applet = NULL;
					gchar* _tmp9_;
					const gchar* _tmp10_;
					_tmp9_ = g_find_program_in_path ("nm-applet");
					test_nm_applet = _tmp9_;
					_tmp10_ = test_nm_applet;
					if (_tmp10_ != NULL) {
						gchar* create_command = NULL;
						gchar* _tmp11_;
						const gchar* _tmp12_;
						gchar** _tmp13_;
						gchar** _tmp14_;
						gchar** _tmp15_;
						gint _tmp15__length1;
						lxsession_app_object_set_name ((LxsessionAppObject*) self, "nm-applet");
						_tmp11_ = g_strdup ("nm-applet");
						create_command = _tmp11_;
						_tmp12_ = create_command;
						_tmp14_ = _tmp13_ = lxsession_parse_command (_tmp12_, " ", 0);
						_tmp15_ = _tmp14_;
						_tmp15__length1 = _vala_array_length (_tmp13_);
						lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp15_, _vala_array_length (_tmp13_));
						_tmp15_ = (_vala_array_free (_tmp15_, _tmp15__length1, (GDestroyNotify) g_free), NULL);
						_g_free0 (create_command);
						_g_free0 (test_nm_applet);
						break;
					} else {
						gchar* test_wicd = NULL;
						gchar* _tmp16_;
						const gchar* _tmp17_;
						_tmp16_ = g_find_program_in_path ("wicd");
						test_wicd = _tmp16_;
						_tmp17_ = test_wicd;
						if (_tmp17_ != NULL) {
							gchar* create_command = NULL;
							gchar* _tmp18_;
							const gchar* _tmp19_;
							gchar** _tmp20_;
							gchar** _tmp21_;
							gchar** _tmp22_;
							gint _tmp22__length1;
							lxsession_app_object_set_name ((LxsessionAppObject*) self, "wicd");
							_tmp18_ = g_strdup ("wicd");
							create_command = _tmp18_;
							_tmp19_ = create_command;
							_tmp21_ = _tmp20_ = lxsession_parse_command (_tmp19_, " ", 0);
							_tmp22_ = _tmp21_;
							_tmp22__length1 = _vala_array_length (_tmp20_);
							lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp22_, _vala_array_length (_tmp20_));
							_tmp22_ = (_vala_array_free (_tmp22_, _tmp22__length1, (GDestroyNotify) g_free), NULL);
							_g_free0 (create_command);
							_g_free0 (test_wicd);
							_g_free0 (test_nm_applet);
							break;
						}
						_g_free0 (test_wicd);
					}
					_g_free0 (test_nm_applet);
				}
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp23_;
				gchar** _tmp24_;
				gchar** _tmp25_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp26_;
				gint _tmp26__length1;
				const gchar* _tmp27_;
				gchar** _tmp28_;
				gint _tmp28__length1;
				_tmp23_ = self->priv->network_command;
				_tmp25_ = _tmp24_ = lxsession_parse_command (_tmp23_, " ", 0);
				create_command = _tmp25_;
				create_command_length1 = _vala_array_length (_tmp24_);
				_create_command_size_ = create_command_length1;
				_tmp26_ = create_command;
				_tmp26__length1 = create_command_length1;
				_tmp27_ = _tmp26_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp27_);
				_tmp28_ = create_command;
				_tmp28__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp28_, _tmp28__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
	lxsession_app_object_set_guard ((LxsessionAppObject*) self, TRUE);
}

static void
lxsession_network_gui_app_class_init (LxsessionNetworkGuiAppClass * klass,
                                      gpointer klass_data)
{
	lxsession_network_gui_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionNetworkGuiApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_network_gui_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_network_gui_app_finalize;
}

static void
lxsession_network_gui_app_instance_init (LxsessionNetworkGuiApp * self,
                                         gpointer klass)
{
	self->priv = lxsession_network_gui_app_get_instance_private (self);
}

static void
lxsession_network_gui_app_finalize (GObject * obj)
{
	LxsessionNetworkGuiApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_NETWORK_GUI_APP, LxsessionNetworkGuiApp);
	_g_free0 (self->priv->network_command);
	_g_free0 (self->priv->laptop_mode);
	G_OBJECT_CLASS (lxsession_network_gui_app_parent_class)->finalize (obj);
}

static GType
lxsession_network_gui_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionNetworkGuiAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_network_gui_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionNetworkGuiApp), 0, (GInstanceInitFunc) lxsession_network_gui_app_instance_init, NULL };
	GType lxsession_network_gui_app_type_id;
	lxsession_network_gui_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionNetworkGuiApp", &g_define_type_info, 0);
	LxsessionNetworkGuiApp_private_offset = g_type_add_instance_private (lxsession_network_gui_app_type_id, sizeof (LxsessionNetworkGuiAppPrivate));
	return lxsession_network_gui_app_type_id;
}

GType
lxsession_network_gui_app_get_type (void)
{
	static volatile gsize lxsession_network_gui_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_network_gui_app_type_id__once)) {
		GType lxsession_network_gui_app_type_id;
		lxsession_network_gui_app_type_id = lxsession_network_gui_app_get_type_once ();
		g_once_init_leave (&lxsession_network_gui_app_type_id__once, lxsession_network_gui_app_type_id);
	}
	return lxsession_network_gui_app_type_id__once;
}

static inline gpointer
lxsession_audio_manager_app_get_instance_private (LxsessionAudioManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionAudioManagerApp_private_offset);
}

LxsessionAudioManagerApp*
lxsession_audio_manager_app_construct (GType object_type)
{
	LxsessionAudioManagerApp * self = NULL;
	self = (LxsessionAudioManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionAudioManagerApp*
lxsession_audio_manager_app_new (void)
{
	return lxsession_audio_manager_app_construct (LXSESSION_TYPE_AUDIO_MANAGER_APP);
}

static void
lxsession_audio_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionAudioManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	static GQuark _tmp4_label0 = 0;
	self = (LxsessionAudioManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "audio_manager", "command");
	_g_free0 (self->priv->audiomanager_command);
	self->priv->audiomanager_command = _tmp1_;
	_tmp2_ = self->priv->audiomanager_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	if (_tmp5_ == ((0 != _tmp4_label0) ? _tmp4_label0 : (_tmp4_label0 = g_quark_from_static_string ("alsamixer")))) {
		switch (0) {
			default:
			{
				gchar* create_command = NULL;
				gchar* _tmp6_;
				const gchar* _tmp7_;
				gchar** _tmp8_;
				gchar** _tmp9_;
				gchar** _tmp10_;
				gint _tmp10__length1;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, "alsamixer");
				_tmp6_ = g_strdup ("xterm -e alsamixer");
				create_command = _tmp6_;
				_tmp7_ = create_command;
				_tmp9_ = _tmp8_ = lxsession_parse_command (_tmp7_, " ", 0);
				_tmp10_ = _tmp9_;
				_tmp10__length1 = _vala_array_length (_tmp8_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp10_, _vala_array_length (_tmp8_));
				_tmp10_ = (_vala_array_free (_tmp10_, _tmp10__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp11_;
				gchar** _tmp12_;
				gchar** _tmp13_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp14_;
				gint _tmp14__length1;
				const gchar* _tmp15_;
				gchar** _tmp16_;
				gint _tmp16__length1;
				_tmp11_ = self->priv->audiomanager_command;
				_tmp13_ = _tmp12_ = lxsession_parse_command (_tmp11_, " ", 0);
				create_command = _tmp13_;
				create_command_length1 = _vala_array_length (_tmp12_);
				_create_command_size_ = create_command_length1;
				_tmp14_ = create_command;
				_tmp14__length1 = create_command_length1;
				_tmp15_ = _tmp14_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp15_);
				_tmp16_ = create_command;
				_tmp16__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp16_, _tmp16__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

static void
lxsession_audio_manager_app_class_init (LxsessionAudioManagerAppClass * klass,
                                        gpointer klass_data)
{
	lxsession_audio_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionAudioManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_audio_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_audio_manager_app_finalize;
}

static void
lxsession_audio_manager_app_instance_init (LxsessionAudioManagerApp * self,
                                           gpointer klass)
{
	self->priv = lxsession_audio_manager_app_get_instance_private (self);
}

static void
lxsession_audio_manager_app_finalize (GObject * obj)
{
	LxsessionAudioManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_AUDIO_MANAGER_APP, LxsessionAudioManagerApp);
	_g_free0 (self->priv->audiomanager_command);
	G_OBJECT_CLASS (lxsession_audio_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_audio_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionAudioManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_audio_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionAudioManagerApp), 0, (GInstanceInitFunc) lxsession_audio_manager_app_instance_init, NULL };
	GType lxsession_audio_manager_app_type_id;
	lxsession_audio_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionAudioManagerApp", &g_define_type_info, 0);
	LxsessionAudioManagerApp_private_offset = g_type_add_instance_private (lxsession_audio_manager_app_type_id, sizeof (LxsessionAudioManagerAppPrivate));
	return lxsession_audio_manager_app_type_id;
}

GType
lxsession_audio_manager_app_get_type (void)
{
	static volatile gsize lxsession_audio_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_audio_manager_app_type_id__once)) {
		GType lxsession_audio_manager_app_type_id;
		lxsession_audio_manager_app_type_id = lxsession_audio_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_audio_manager_app_type_id__once, lxsession_audio_manager_app_type_id);
	}
	return lxsession_audio_manager_app_type_id__once;
}

static inline gpointer
lxsession_quit_manager_app_get_instance_private (LxsessionQuitManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionQuitManagerApp_private_offset);
}

LxsessionQuitManagerApp*
lxsession_quit_manager_app_construct (GType object_type)
{
	LxsessionQuitManagerApp * self = NULL;
	self = (LxsessionQuitManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionQuitManagerApp*
lxsession_quit_manager_app_new (void)
{
	return lxsession_quit_manager_app_construct (LXSESSION_TYPE_QUIT_MANAGER_APP);
}

static void
lxsession_quit_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionQuitManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	LxsessionLxsessionConfigKeyFile* _tmp2_;
	gchar* _tmp3_;
	LxsessionLxsessionConfigKeyFile* _tmp4_;
	gchar* _tmp5_;
	const gchar* _tmp6_;
	const gchar* _tmp7_;
	GQuark _tmp9_ = 0U;
	static GQuark _tmp8_label0 = 0;
	self = (LxsessionQuitManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "quit_manager", "command");
	_g_free0 (self->priv->quitmanager_command);
	self->priv->quitmanager_command = _tmp1_;
	_tmp2_ = lxsession_global_settings;
	_tmp3_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp2_, "Session", "quit_manager", "image");
	_g_free0 (self->priv->quitmanager_image);
	self->priv->quitmanager_image = _tmp3_;
	_tmp4_ = lxsession_global_settings;
	_tmp5_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp4_, "Session", "quit_manager", "layout");
	_g_free0 (self->priv->quitmanager_layout);
	self->priv->quitmanager_layout = _tmp5_;
	_tmp6_ = self->priv->quitmanager_command;
	_tmp7_ = _tmp6_;
	_tmp9_ = (NULL == _tmp7_) ? 0 : g_quark_from_string (_tmp7_);
	if (_tmp9_ == ((0 != _tmp8_label0) ? _tmp8_label0 : (_tmp8_label0 = g_quark_from_static_string ("luxesession-logout")))) {
		switch (0) {
			default:
			{
				gchar* create_command = NULL;
				const gchar* _tmp10_;
				gchar* _tmp11_;
				gchar* _tmp12_;
				gchar* _tmp13_;
				gchar* _tmp14_;
				const gchar* _tmp15_;
				gchar* _tmp16_;
				gchar* _tmp17_;
				const gchar* _tmp18_;
				gchar** _tmp19_;
				gchar** _tmp20_;
				gchar** _tmp21_;
				gint _tmp21__length1;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, "luxesession-logout");
				_tmp10_ = self->priv->quitmanager_image;
				_tmp11_ = g_strconcat ("luxesession-logout --banner ", _tmp10_, NULL);
				_tmp12_ = _tmp11_;
				_tmp13_ = g_strconcat (_tmp12_, " --side=", NULL);
				_tmp14_ = _tmp13_;
				_tmp15_ = self->priv->quitmanager_layout;
				_tmp16_ = g_strconcat (_tmp14_, _tmp15_, NULL);
				_tmp17_ = _tmp16_;
				_g_free0 (_tmp14_);
				_g_free0 (_tmp12_);
				create_command = _tmp17_;
				_tmp18_ = create_command;
				_tmp20_ = _tmp19_ = lxsession_parse_command (_tmp18_, " ", 0);
				_tmp21_ = _tmp20_;
				_tmp21__length1 = _vala_array_length (_tmp19_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp21_, _vala_array_length (_tmp19_));
				_tmp21_ = (_vala_array_free (_tmp21_, _tmp21__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp22_;
				gchar** _tmp23_;
				gchar** _tmp24_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp25_;
				gint _tmp25__length1;
				const gchar* _tmp26_;
				gchar** _tmp27_;
				gint _tmp27__length1;
				_tmp22_ = self->priv->quitmanager_command;
				_tmp24_ = _tmp23_ = lxsession_parse_command (_tmp22_, " ", 0);
				create_command = _tmp24_;
				create_command_length1 = _vala_array_length (_tmp23_);
				_create_command_size_ = create_command_length1;
				_tmp25_ = create_command;
				_tmp25__length1 = create_command_length1;
				_tmp26_ = _tmp25_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp26_);
				_tmp27_ = create_command;
				_tmp27__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp27_, _tmp27__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

static void
lxsession_quit_manager_app_class_init (LxsessionQuitManagerAppClass * klass,
                                       gpointer klass_data)
{
	lxsession_quit_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionQuitManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_quit_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_quit_manager_app_finalize;
}

static void
lxsession_quit_manager_app_instance_init (LxsessionQuitManagerApp * self,
                                          gpointer klass)
{
	self->priv = lxsession_quit_manager_app_get_instance_private (self);
}

static void
lxsession_quit_manager_app_finalize (GObject * obj)
{
	LxsessionQuitManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_QUIT_MANAGER_APP, LxsessionQuitManagerApp);
	_g_free0 (self->priv->quitmanager_command);
	_g_free0 (self->priv->quitmanager_image);
	_g_free0 (self->priv->quitmanager_layout);
	G_OBJECT_CLASS (lxsession_quit_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_quit_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionQuitManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_quit_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionQuitManagerApp), 0, (GInstanceInitFunc) lxsession_quit_manager_app_instance_init, NULL };
	GType lxsession_quit_manager_app_type_id;
	lxsession_quit_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionQuitManagerApp", &g_define_type_info, 0);
	LxsessionQuitManagerApp_private_offset = g_type_add_instance_private (lxsession_quit_manager_app_type_id, sizeof (LxsessionQuitManagerAppPrivate));
	return lxsession_quit_manager_app_type_id;
}

GType
lxsession_quit_manager_app_get_type (void)
{
	static volatile gsize lxsession_quit_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_quit_manager_app_type_id__once)) {
		GType lxsession_quit_manager_app_type_id;
		lxsession_quit_manager_app_type_id = lxsession_quit_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_quit_manager_app_type_id__once, lxsession_quit_manager_app_type_id);
	}
	return lxsession_quit_manager_app_type_id__once;
}

static inline gpointer
lxsession_workspace_manager_app_get_instance_private (LxsessionWorkspaceManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionWorkspaceManagerApp_private_offset);
}

LxsessionWorkspaceManagerApp*
lxsession_workspace_manager_app_construct (GType object_type)
{
	LxsessionWorkspaceManagerApp * self = NULL;
	self = (LxsessionWorkspaceManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionWorkspaceManagerApp*
lxsession_workspace_manager_app_new (void)
{
	return lxsession_workspace_manager_app_construct (LXSESSION_TYPE_WORKSPACE_MANAGER_APP);
}

static void
lxsession_workspace_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionWorkspaceManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	static GQuark _tmp4_label0 = 0;
	self = (LxsessionWorkspaceManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "workspace_manager", "command");
	_g_free0 (self->priv->workspacemanager_command);
	self->priv->workspacemanager_command = _tmp1_;
	_tmp2_ = self->priv->workspacemanager_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	if (_tmp5_ == ((0 != _tmp4_label0) ? _tmp4_label0 : (_tmp4_label0 = g_quark_from_static_string ("obconf")))) {
		switch (0) {
			default:
			{
				gchar* create_command = NULL;
				gchar* _tmp6_;
				const gchar* _tmp7_;
				gchar** _tmp8_;
				gchar** _tmp9_;
				gchar** _tmp10_;
				gint _tmp10__length1;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, "obconf");
				_tmp6_ = g_strdup ("obconf --tab 6");
				create_command = _tmp6_;
				_tmp7_ = create_command;
				_tmp9_ = _tmp8_ = lxsession_parse_command (_tmp7_, " ", 0);
				_tmp10_ = _tmp9_;
				_tmp10__length1 = _vala_array_length (_tmp8_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp10_, _vala_array_length (_tmp8_));
				_tmp10_ = (_vala_array_free (_tmp10_, _tmp10__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp11_;
				gchar** _tmp12_;
				gchar** _tmp13_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp14_;
				gint _tmp14__length1;
				const gchar* _tmp15_;
				gchar** _tmp16_;
				gint _tmp16__length1;
				_tmp11_ = self->priv->workspacemanager_command;
				_tmp13_ = _tmp12_ = lxsession_parse_command (_tmp11_, " ", 0);
				create_command = _tmp13_;
				create_command_length1 = _vala_array_length (_tmp12_);
				_create_command_size_ = create_command_length1;
				_tmp14_ = create_command;
				_tmp14__length1 = create_command_length1;
				_tmp15_ = _tmp14_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp15_);
				_tmp16_ = create_command;
				_tmp16__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp16_, _tmp16__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

static void
lxsession_workspace_manager_app_class_init (LxsessionWorkspaceManagerAppClass * klass,
                                            gpointer klass_data)
{
	lxsession_workspace_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionWorkspaceManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_workspace_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_workspace_manager_app_finalize;
}

static void
lxsession_workspace_manager_app_instance_init (LxsessionWorkspaceManagerApp * self,
                                               gpointer klass)
{
	self->priv = lxsession_workspace_manager_app_get_instance_private (self);
}

static void
lxsession_workspace_manager_app_finalize (GObject * obj)
{
	LxsessionWorkspaceManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_WORKSPACE_MANAGER_APP, LxsessionWorkspaceManagerApp);
	_g_free0 (self->priv->workspacemanager_command);
	G_OBJECT_CLASS (lxsession_workspace_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_workspace_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionWorkspaceManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_workspace_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionWorkspaceManagerApp), 0, (GInstanceInitFunc) lxsession_workspace_manager_app_instance_init, NULL };
	GType lxsession_workspace_manager_app_type_id;
	lxsession_workspace_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionWorkspaceManagerApp", &g_define_type_info, 0);
	LxsessionWorkspaceManagerApp_private_offset = g_type_add_instance_private (lxsession_workspace_manager_app_type_id, sizeof (LxsessionWorkspaceManagerAppPrivate));
	return lxsession_workspace_manager_app_type_id;
}

GType
lxsession_workspace_manager_app_get_type (void)
{
	static volatile gsize lxsession_workspace_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_workspace_manager_app_type_id__once)) {
		GType lxsession_workspace_manager_app_type_id;
		lxsession_workspace_manager_app_type_id = lxsession_workspace_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_workspace_manager_app_type_id__once, lxsession_workspace_manager_app_type_id);
	}
	return lxsession_workspace_manager_app_type_id__once;
}

static inline gpointer
lxsession_launcher_manager_app_get_instance_private (LxsessionLauncherManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionLauncherManagerApp_private_offset);
}

LxsessionLauncherManagerApp*
lxsession_launcher_manager_app_construct (GType object_type)
{
	LxsessionLauncherManagerApp * self = NULL;
	self = (LxsessionLauncherManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionLauncherManagerApp*
lxsession_launcher_manager_app_new (void)
{
	return lxsession_launcher_manager_app_construct (LXSESSION_TYPE_LAUNCHER_MANAGER_APP);
}

static void
lxsession_launcher_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionLauncherManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	static GQuark _tmp4_label0 = 0;
	self = (LxsessionLauncherManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "launcher_manager", "command");
	_g_free0 (self->priv->launchermanager_command);
	self->priv->launchermanager_command = _tmp1_;
	_tmp2_ = self->priv->launchermanager_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	if (_tmp5_ == ((0 != _tmp4_label0) ? _tmp4_label0 : (_tmp4_label0 = g_quark_from_static_string ("luxepanelctl")))) {
		switch (0) {
			default:
			{
				gchar* create_command = NULL;
				gchar* _tmp6_;
				const gchar* _tmp7_;
				gchar** _tmp8_;
				gchar** _tmp9_;
				gchar** _tmp10_;
				gint _tmp10__length1;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, "luxepanelctl");
				_tmp6_ = g_strdup ("luxepanelctl run");
				create_command = _tmp6_;
				_tmp7_ = create_command;
				_tmp9_ = _tmp8_ = lxsession_parse_command (_tmp7_, " ", 0);
				_tmp10_ = _tmp9_;
				_tmp10__length1 = _vala_array_length (_tmp8_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp10_, _vala_array_length (_tmp8_));
				_tmp10_ = (_vala_array_free (_tmp10_, _tmp10__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp11_;
				gchar** _tmp12_;
				gchar** _tmp13_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp14_;
				gint _tmp14__length1;
				const gchar* _tmp15_;
				gchar** _tmp16_;
				gint _tmp16__length1;
				_tmp11_ = self->priv->launchermanager_command;
				_tmp13_ = _tmp12_ = lxsession_parse_command (_tmp11_, " ", 0);
				create_command = _tmp13_;
				create_command_length1 = _vala_array_length (_tmp12_);
				_create_command_size_ = create_command_length1;
				_tmp14_ = create_command;
				_tmp14__length1 = create_command_length1;
				_tmp15_ = _tmp14_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp15_);
				_tmp16_ = create_command;
				_tmp16__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp16_, _tmp16__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

void
lxsession_launcher_manager_app_autostart_launch (LxsessionLauncherManagerApp* self)
{
	const gchar* _tmp0_;
	const gchar* _tmp1_;
	GQuark _tmp3_ = 0U;
	static GQuark _tmp2_label0 = 0;
	g_return_if_fail (self != NULL);
	_tmp0_ = self->priv->launchermanager_command;
	_tmp1_ = _tmp0_;
	_tmp3_ = (NULL == _tmp1_) ? 0 : g_quark_from_string (_tmp1_);
	if (_tmp3_ == ((0 != _tmp2_label0) ? _tmp2_label0 : (_tmp2_label0 = g_quark_from_static_string ("synapse")))) {
		switch (0) {
			default:
			{
				gchar* create_autostart_command = NULL;
				gchar* _tmp4_;
				const gchar* _tmp5_;
				_tmp4_ = g_strdup ("synapse --startup");
				create_autostart_command = _tmp4_;
				_tmp5_ = create_autostart_command;
				lxsession_luxesession_spawn_command_line_async (_tmp5_);
				_g_free0 (create_autostart_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				lxsession_app_object_launch ((LxsessionAppObject*) self);
				break;
			}
		}
	}
}

static void
lxsession_launcher_manager_app_class_init (LxsessionLauncherManagerAppClass * klass,
                                           gpointer klass_data)
{
	lxsession_launcher_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionLauncherManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_launcher_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_launcher_manager_app_finalize;
}

static void
lxsession_launcher_manager_app_instance_init (LxsessionLauncherManagerApp * self,
                                              gpointer klass)
{
	self->priv = lxsession_launcher_manager_app_get_instance_private (self);
}

static void
lxsession_launcher_manager_app_finalize (GObject * obj)
{
	LxsessionLauncherManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_LAUNCHER_MANAGER_APP, LxsessionLauncherManagerApp);
	_g_free0 (self->priv->launchermanager_command);
	G_OBJECT_CLASS (lxsession_launcher_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_launcher_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionLauncherManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_launcher_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionLauncherManagerApp), 0, (GInstanceInitFunc) lxsession_launcher_manager_app_instance_init, NULL };
	GType lxsession_launcher_manager_app_type_id;
	lxsession_launcher_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionLauncherManagerApp", &g_define_type_info, 0);
	LxsessionLauncherManagerApp_private_offset = g_type_add_instance_private (lxsession_launcher_manager_app_type_id, sizeof (LxsessionLauncherManagerAppPrivate));
	return lxsession_launcher_manager_app_type_id;
}

GType
lxsession_launcher_manager_app_get_type (void)
{
	static volatile gsize lxsession_launcher_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_launcher_manager_app_type_id__once)) {
		GType lxsession_launcher_manager_app_type_id;
		lxsession_launcher_manager_app_type_id = lxsession_launcher_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_launcher_manager_app_type_id__once, lxsession_launcher_manager_app_type_id);
	}
	return lxsession_launcher_manager_app_type_id__once;
}

static inline gpointer
lxsession_terminal_manager_app_get_instance_private (LxsessionTerminalManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionTerminalManagerApp_private_offset);
}

LxsessionTerminalManagerApp*
lxsession_terminal_manager_app_construct (GType object_type)
{
	LxsessionTerminalManagerApp * self = NULL;
	self = (LxsessionTerminalManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionTerminalManagerApp*
lxsession_terminal_manager_app_new (void)
{
	return lxsession_terminal_manager_app_construct (LXSESSION_TYPE_TERMINAL_MANAGER_APP);
}

static void
lxsession_terminal_manager_app_real_read_config_settings (LxsessionAppObject* base)
{
	LxsessionTerminalManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	self = (LxsessionTerminalManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "terminal_manager", "command");
	_g_free0 (self->priv->terminalmanager_command);
	self->priv->terminalmanager_command = _tmp1_;
}

static void
lxsession_terminal_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionTerminalManagerApp * self;
	const gchar* _tmp0_;
	const gchar* _tmp1_;
	GQuark _tmp3_ = 0U;
	static GQuark _tmp2_label0 = 0;
	self = (LxsessionTerminalManagerApp*) base;
	_tmp0_ = self->priv->terminalmanager_command;
	_tmp1_ = _tmp0_;
	_tmp3_ = (NULL == _tmp1_) ? 0 : g_quark_from_string (_tmp1_);
	if (_tmp3_ == ((0 != _tmp2_label0) ? _tmp2_label0 : (_tmp2_label0 = g_quark_from_static_string ("luxeterm")))) {
		switch (0) {
			default:
			{
				gchar* create_command = NULL;
				gchar* _tmp4_;
				const gchar* _tmp5_;
				gchar** _tmp6_;
				gchar** _tmp7_;
				gchar** _tmp8_;
				gint _tmp8__length1;
				lxsession_app_object_set_name ((LxsessionAppObject*) self, "luxeterm");
				_tmp4_ = g_strdup ("luxeterm -e");
				create_command = _tmp4_;
				_tmp5_ = create_command;
				_tmp7_ = _tmp6_ = lxsession_parse_command (_tmp5_, " ", 0);
				_tmp8_ = _tmp7_;
				_tmp8__length1 = _vala_array_length (_tmp6_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp8_, _vala_array_length (_tmp6_));
				_tmp8_ = (_vala_array_free (_tmp8_, _tmp8__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp9_;
				gchar** _tmp10_;
				gchar** _tmp11_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp12_;
				gint _tmp12__length1;
				const gchar* _tmp13_;
				gchar** _tmp14_;
				gint _tmp14__length1;
				_tmp9_ = self->priv->terminalmanager_command;
				_tmp11_ = _tmp10_ = lxsession_parse_command (_tmp9_, " ", 0);
				create_command = _tmp11_;
				create_command_length1 = _vala_array_length (_tmp10_);
				_create_command_size_ = create_command_length1;
				_tmp12_ = create_command;
				_tmp12__length1 = create_command_length1;
				_tmp13_ = _tmp12_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp13_);
				_tmp14_ = create_command;
				_tmp14__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp14_, _tmp14__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

void
lxsession_terminal_manager_app_launch (LxsessionTerminalManagerApp* self,
                                       const gchar* argument)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (argument != NULL);
	if (g_strcmp0 (argument, "") == 0) {
		argument = NULL;
	}
	lxsession_app_object_generic_launch ((LxsessionAppObject*) self, argument);
}

static void
lxsession_terminal_manager_app_class_init (LxsessionTerminalManagerAppClass * klass,
                                           gpointer klass_data)
{
	lxsession_terminal_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionTerminalManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_config_settings = (void (*) (LxsessionAppObject*)) lxsession_terminal_manager_app_real_read_config_settings;
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_terminal_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_terminal_manager_app_finalize;
}

static void
lxsession_terminal_manager_app_instance_init (LxsessionTerminalManagerApp * self,
                                              gpointer klass)
{
	self->priv = lxsession_terminal_manager_app_get_instance_private (self);
}

static void
lxsession_terminal_manager_app_finalize (GObject * obj)
{
	LxsessionTerminalManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_TERMINAL_MANAGER_APP, LxsessionTerminalManagerApp);
	_g_free0 (self->priv->terminalmanager_command);
	G_OBJECT_CLASS (lxsession_terminal_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_terminal_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionTerminalManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_terminal_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionTerminalManagerApp), 0, (GInstanceInitFunc) lxsession_terminal_manager_app_instance_init, NULL };
	GType lxsession_terminal_manager_app_type_id;
	lxsession_terminal_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionTerminalManagerApp", &g_define_type_info, 0);
	LxsessionTerminalManagerApp_private_offset = g_type_add_instance_private (lxsession_terminal_manager_app_type_id, sizeof (LxsessionTerminalManagerAppPrivate));
	return lxsession_terminal_manager_app_type_id;
}

GType
lxsession_terminal_manager_app_get_type (void)
{
	static volatile gsize lxsession_terminal_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_terminal_manager_app_type_id__once)) {
		GType lxsession_terminal_manager_app_type_id;
		lxsession_terminal_manager_app_type_id = lxsession_terminal_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_terminal_manager_app_type_id__once, lxsession_terminal_manager_app_type_id);
	}
	return lxsession_terminal_manager_app_type_id__once;
}

static inline gpointer
lxsession_proxy_manager_app_get_instance_private (LxsessionProxyManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionProxyManagerApp_private_offset);
}

LxsessionProxyManagerApp*
lxsession_proxy_manager_app_construct (GType object_type)
{
	LxsessionProxyManagerApp * self = NULL;
	self = (LxsessionProxyManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionProxyManagerApp*
lxsession_proxy_manager_app_new (void)
{
	return lxsession_proxy_manager_app_construct (LXSESSION_TYPE_PROXY_MANAGER_APP);
}

static void
lxsession_proxy_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionProxyManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	LxsessionLxsessionConfigKeyFile* _tmp2_;
	gchar* _tmp3_;
	const gchar* _tmp4_;
	const gchar* _tmp5_;
	GQuark _tmp7_ = 0U;
	static GQuark _tmp6_label0 = 0;
	self = (LxsessionProxyManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "proxy_manager", "command");
	_g_free0 (self->priv->proxymanager_command);
	self->priv->proxymanager_command = _tmp1_;
	_tmp2_ = lxsession_global_settings;
	_tmp3_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp2_, "Session", "proxy_manager", "http");
	_g_free0 (self->priv->proxymanager_http);
	self->priv->proxymanager_http = _tmp3_;
	_tmp4_ = self->priv->proxymanager_command;
	_tmp5_ = _tmp4_;
	_tmp7_ = (NULL == _tmp5_) ? 0 : g_quark_from_string (_tmp5_);
	if (_tmp7_ == ((0 != _tmp6_label0) ? _tmp6_label0 : (_tmp6_label0 = g_quark_from_static_string ("build-in")))) {
		switch (0) {
			default:
			{
				const gchar* _tmp8_;
				const gchar* _tmp9_;
				GQuark _tmp11_ = 0U;
				static GQuark _tmp10_label0 = 0;
				static GQuark _tmp10_label1 = 0;
				static GQuark _tmp10_label2 = 0;
				_tmp8_ = self->priv->proxymanager_http;
				_tmp9_ = _tmp8_;
				_tmp11_ = (NULL == _tmp9_) ? 0 : g_quark_from_string (_tmp9_);
				if (_tmp11_ == ((0 != _tmp10_label0) ? _tmp10_label0 : (_tmp10_label0 = g_quark_from_static_string (NULL)))) {
					switch (0) {
						default:
						{
							break;
						}
					}
				} else if (_tmp11_ == ((0 != _tmp10_label1) ? _tmp10_label1 : (_tmp10_label1 = g_quark_from_static_string ("")))) {
					switch (0) {
						default:
						{
							break;
						}
					}
				} else if (_tmp11_ == ((0 != _tmp10_label2) ? _tmp10_label2 : (_tmp10_label2 = g_quark_from_static_string (" ")))) {
					switch (0) {
						default:
						{
							break;
						}
					}
				} else {
					switch (0) {
						default:
						{
							const gchar* _tmp12_;
							_tmp12_ = self->priv->proxymanager_http;
							g_setenv ("HTTP_PROXY", _tmp12_, TRUE);
							break;
						}
					}
				}
				break;
			}
		}
	}
}

static void
lxsession_proxy_manager_app_class_init (LxsessionProxyManagerAppClass * klass,
                                        gpointer klass_data)
{
	lxsession_proxy_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionProxyManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_proxy_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_proxy_manager_app_finalize;
}

static void
lxsession_proxy_manager_app_instance_init (LxsessionProxyManagerApp * self,
                                           gpointer klass)
{
	self->priv = lxsession_proxy_manager_app_get_instance_private (self);
}

static void
lxsession_proxy_manager_app_finalize (GObject * obj)
{
	LxsessionProxyManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_PROXY_MANAGER_APP, LxsessionProxyManagerApp);
	_g_free0 (self->priv->proxymanager_command);
	_g_free0 (self->priv->proxymanager_http);
	G_OBJECT_CLASS (lxsession_proxy_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_proxy_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionProxyManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_proxy_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionProxyManagerApp), 0, (GInstanceInitFunc) lxsession_proxy_manager_app_instance_init, NULL };
	GType lxsession_proxy_manager_app_type_id;
	lxsession_proxy_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionProxyManagerApp", &g_define_type_info, 0);
	LxsessionProxyManagerApp_private_offset = g_type_add_instance_private (lxsession_proxy_manager_app_type_id, sizeof (LxsessionProxyManagerAppPrivate));
	return lxsession_proxy_manager_app_type_id;
}

GType
lxsession_proxy_manager_app_get_type (void)
{
	static volatile gsize lxsession_proxy_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_proxy_manager_app_type_id__once)) {
		GType lxsession_proxy_manager_app_type_id;
		lxsession_proxy_manager_app_type_id = lxsession_proxy_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_proxy_manager_app_type_id__once, lxsession_proxy_manager_app_type_id);
	}
	return lxsession_proxy_manager_app_type_id__once;
}

static inline gpointer
lxsession_a11y_app_get_instance_private (LxsessionA11yApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionA11yApp_private_offset);
}

LxsessionA11yApp*
lxsession_a11y_app_construct (GType object_type)
{
	LxsessionA11yApp * self = NULL;
	self = (LxsessionA11yApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionA11yApp*
lxsession_a11y_app_new (void)
{
	return lxsession_a11y_app_construct (LXSESSION_TYPE_A11Y_APP);
}

static void
lxsession_a11y_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionA11yApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	static GQuark _tmp4_label0 = 0;
	static GQuark _tmp4_label1 = 0;
	static GQuark _tmp4_label2 = 0;
	static GQuark _tmp4_label3 = 0;
	self = (LxsessionA11yApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "a11y", "command");
	_g_free0 (self->priv->a11y_command);
	self->priv->a11y_command = _tmp1_;
	_tmp2_ = self->priv->a11y_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	if (_tmp5_ == ((0 != _tmp4_label0) ? _tmp4_label0 : (_tmp4_label0 = g_quark_from_static_string (NULL)))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label1) ? _tmp4_label1 : (_tmp4_label1 = g_quark_from_static_string ("")))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label2) ? _tmp4_label2 : (_tmp4_label2 = g_quark_from_static_string (" ")))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label3) ? _tmp4_label3 : (_tmp4_label3 = g_quark_from_static_string ("gnome")))) {
		switch (0) {
			default:
			{
				gchar* tmp_command = NULL;
				gchar* _tmp6_;
				gchar** create_command = NULL;
				const gchar* _tmp7_;
				gchar** _tmp8_;
				gchar** _tmp9_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp10_;
				gint _tmp10__length1;
				const gchar* _tmp11_;
				gchar** _tmp12_;
				gint _tmp12__length1;
				_tmp6_ = g_find_program_in_path ("at-spi-bus-launcher");
				if (_tmp6_ == NULL)
					_tmp6_ = g_strdup ("at-spi-bus-launcher");
				else {
					gchar *program = _tmp6_;
					_tmp6_ = g_strconcat (program, " --launch-immediately", NULL);
					g_free (program);
				}
				tmp_command = _tmp6_;
				_tmp7_ = tmp_command;
				_tmp9_ = _tmp8_ = lxsession_parse_command (_tmp7_, " ", 0);
				create_command = _tmp9_;
				create_command_length1 = _vala_array_length (_tmp8_);
				_create_command_size_ = create_command_length1;
				_tmp10_ = create_command;
				_tmp10__length1 = create_command_length1;
				_tmp11_ = _tmp10_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp11_);
				_tmp12_ = create_command;
				_tmp12__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp12_, _tmp12__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (tmp_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp13_;
				gchar** _tmp14_;
				gchar** _tmp15_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp16_;
				gint _tmp16__length1;
				const gchar* _tmp17_;
				gchar** _tmp18_;
				gint _tmp18__length1;
				_tmp13_ = self->priv->a11y_command;
				_tmp15_ = _tmp14_ = lxsession_parse_command (_tmp13_, " ", 0);
				create_command = _tmp15_;
				create_command_length1 = _vala_array_length (_tmp14_);
				_create_command_size_ = create_command_length1;
				_tmp16_ = create_command;
				_tmp16__length1 = create_command_length1;
				_tmp17_ = _tmp16_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp17_);
				_tmp18_ = create_command;
				_tmp18__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp18_, _tmp18__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

static void
lxsession_a11y_app_class_init (LxsessionA11yAppClass * klass,
                               gpointer klass_data)
{
	lxsession_a11y_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionA11yApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_a11y_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_a11y_app_finalize;
}

static void
lxsession_a11y_app_instance_init (LxsessionA11yApp * self,
                                  gpointer klass)
{
	self->priv = lxsession_a11y_app_get_instance_private (self);
}

static void
lxsession_a11y_app_finalize (GObject * obj)
{
	LxsessionA11yApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_A11Y_APP, LxsessionA11yApp);
	_g_free0 (self->priv->a11y_command);
	G_OBJECT_CLASS (lxsession_a11y_app_parent_class)->finalize (obj);
}

static GType
lxsession_a11y_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionA11yAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_a11y_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionA11yApp), 0, (GInstanceInitFunc) lxsession_a11y_app_instance_init, NULL };
	GType lxsession_a11y_app_type_id;
	lxsession_a11y_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionA11yApp", &g_define_type_info, 0);
	LxsessionA11yApp_private_offset = g_type_add_instance_private (lxsession_a11y_app_type_id, sizeof (LxsessionA11yAppPrivate));
	return lxsession_a11y_app_type_id;
}

GType
lxsession_a11y_app_get_type (void)
{
	static volatile gsize lxsession_a11y_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_a11y_app_type_id__once)) {
		GType lxsession_a11y_app_type_id;
		lxsession_a11y_app_type_id = lxsession_a11y_app_get_type_once ();
		g_once_init_leave (&lxsession_a11y_app_type_id__once, lxsession_a11y_app_type_id);
	}
	return lxsession_a11y_app_type_id__once;
}

static inline gpointer
lxsession_xrandr_app_get_instance_private (LxsessionXrandrApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionXrandrApp_private_offset);
}

LxsessionXrandrApp*
lxsession_xrandr_app_construct (GType object_type)
{
	LxsessionXrandrApp * self = NULL;
	self = (LxsessionXrandrApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionXrandrApp*
lxsession_xrandr_app_new (void)
{
	return lxsession_xrandr_app_construct (LXSESSION_TYPE_XRANDR_APP);
}

static void
lxsession_xrandr_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionXrandrApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	static GQuark _tmp4_label0 = 0;
	static GQuark _tmp4_label1 = 0;
	static GQuark _tmp4_label2 = 0;
	self = (LxsessionXrandrApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "xrandr", "command");
	_g_free0 (self->priv->xrandr_command);
	self->priv->xrandr_command = _tmp1_;
	_tmp2_ = self->priv->xrandr_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	if (_tmp5_ == ((0 != _tmp4_label0) ? _tmp4_label0 : (_tmp4_label0 = g_quark_from_static_string (NULL)))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label1) ? _tmp4_label1 : (_tmp4_label1 = g_quark_from_static_string ("")))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label2) ? _tmp4_label2 : (_tmp4_label2 = g_quark_from_static_string (" ")))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp6_;
				gchar** _tmp7_;
				gchar** _tmp8_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp9_;
				gint _tmp9__length1;
				const gchar* _tmp10_;
				gchar** _tmp11_;
				gint _tmp11__length1;
				_tmp6_ = self->priv->xrandr_command;
				_tmp8_ = _tmp7_ = lxsession_parse_command (_tmp6_, " ", 0);
				create_command = _tmp8_;
				create_command_length1 = _vala_array_length (_tmp7_);
				_create_command_size_ = create_command_length1;
				_tmp9_ = create_command;
				_tmp9__length1 = create_command_length1;
				_tmp10_ = _tmp9_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp10_);
				_tmp11_ = create_command;
				_tmp11__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp11_, _tmp11__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

static void
lxsession_xrandr_app_class_init (LxsessionXrandrAppClass * klass,
                                 gpointer klass_data)
{
	lxsession_xrandr_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionXrandrApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_xrandr_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_xrandr_app_finalize;
}

static void
lxsession_xrandr_app_instance_init (LxsessionXrandrApp * self,
                                    gpointer klass)
{
	self->priv = lxsession_xrandr_app_get_instance_private (self);
}

static void
lxsession_xrandr_app_finalize (GObject * obj)
{
	LxsessionXrandrApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_XRANDR_APP, LxsessionXrandrApp);
	_g_free0 (self->priv->xrandr_command);
	G_OBJECT_CLASS (lxsession_xrandr_app_parent_class)->finalize (obj);
}

static GType
lxsession_xrandr_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionXrandrAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_xrandr_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionXrandrApp), 0, (GInstanceInitFunc) lxsession_xrandr_app_instance_init, NULL };
	GType lxsession_xrandr_app_type_id;
	lxsession_xrandr_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionXrandrApp", &g_define_type_info, 0);
	LxsessionXrandrApp_private_offset = g_type_add_instance_private (lxsession_xrandr_app_type_id, sizeof (LxsessionXrandrAppPrivate));
	return lxsession_xrandr_app_type_id;
}

GType
lxsession_xrandr_app_get_type (void)
{
	static volatile gsize lxsession_xrandr_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_xrandr_app_type_id__once)) {
		GType lxsession_xrandr_app_type_id;
		lxsession_xrandr_app_type_id = lxsession_xrandr_app_get_type_once ();
		g_once_init_leave (&lxsession_xrandr_app_type_id__once, lxsession_xrandr_app_type_id);
	}
	return lxsession_xrandr_app_type_id__once;
}

static inline gpointer
lxsession_keyring_app_get_instance_private (LxsessionKeyringApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionKeyringApp_private_offset);
}

LxsessionKeyringApp*
lxsession_keyring_app_construct (GType object_type)
{
	LxsessionKeyringApp * self = NULL;
	self = (LxsessionKeyringApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionKeyringApp*
lxsession_keyring_app_new (void)
{
	return lxsession_keyring_app_construct (LXSESSION_TYPE_KEYRING_APP);
}

static void
lxsession_keyring_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionKeyringApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	LxsessionLxsessionConfigKeyFile* _tmp2_;
	gchar* _tmp3_;
	const gchar* _tmp4_;
	const gchar* _tmp5_;
	GQuark _tmp7_ = 0U;
	static GQuark _tmp6_label0 = 0;
	static GQuark _tmp6_label1 = 0;
	self = (LxsessionKeyringApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "keyring", "command");
	_g_free0 (self->priv->keyring_command);
	self->priv->keyring_command = _tmp1_;
	_tmp2_ = lxsession_global_settings;
	_tmp3_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp2_, "Session", "keyring", "type");
	_g_free0 (self->priv->keyring_type);
	self->priv->keyring_type = _tmp3_;
	_tmp4_ = self->priv->keyring_command;
	_tmp5_ = _tmp4_;
	_tmp7_ = (NULL == _tmp5_) ? 0 : g_quark_from_string (_tmp5_);
	if (_tmp7_ == ((0 != _tmp6_label0) ? _tmp6_label0 : (_tmp6_label0 = g_quark_from_static_string ("gnome-all")))) {
		switch (0) {
			default:
			{
				gchar* tmp_command = NULL;
				gchar* _tmp8_;
				gchar** create_command = NULL;
				const gchar* _tmp9_;
				gchar** _tmp10_;
				gchar** _tmp11_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp12_;
				gint _tmp12__length1;
				const gchar* _tmp13_;
				gchar** _tmp14_;
				gint _tmp14__length1;
				_tmp8_ = g_strdup ("gnome-keyring-daemon --start --components=pkcs11,secrets,ssh,gpg");
				tmp_command = _tmp8_;
				_tmp9_ = tmp_command;
				_tmp11_ = _tmp10_ = lxsession_parse_command (_tmp9_, " ", 0);
				create_command = _tmp11_;
				create_command_length1 = _vala_array_length (_tmp10_);
				_create_command_size_ = create_command_length1;
				_tmp12_ = create_command;
				_tmp12__length1 = create_command_length1;
				_tmp13_ = _tmp12_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp13_);
				_tmp14_ = create_command;
				_tmp14__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp14_, _tmp14__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (tmp_command);
				break;
			}
		}
	} else if (_tmp7_ == ((0 != _tmp6_label1) ? _tmp6_label1 : (_tmp6_label1 = g_quark_from_static_string ("ssh-agent")))) {
		switch (0) {
			default:
			{
				gchar* tmp_command = NULL;
				gchar* _tmp15_;
				gchar** create_command = NULL;
				const gchar* _tmp16_;
				gchar** _tmp17_;
				gchar** _tmp18_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp19_;
				gint _tmp19__length1;
				const gchar* _tmp20_;
				gchar** _tmp21_;
				gint _tmp21__length1;
				_tmp15_ = g_find_program_in_path ("ssh-agent");
				if (_tmp15_ == NULL)
					_tmp15_ = g_strdup ("ssh-agent");
				else {
					gchar *program = _tmp15_;
					_tmp15_ = g_strconcat (program, " -s", NULL);
					g_free (program);
				}
				tmp_command = _tmp15_;
				_tmp16_ = tmp_command;
				_tmp18_ = _tmp17_ = lxsession_parse_command (_tmp16_, " ", 0);
				create_command = _tmp18_;
				create_command_length1 = _vala_array_length (_tmp17_);
				_create_command_size_ = create_command_length1;
				_tmp19_ = create_command;
				_tmp19__length1 = create_command_length1;
				_tmp20_ = _tmp19_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp20_);
				_tmp21_ = create_command;
				_tmp21__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp21_, _tmp21__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (tmp_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp22_;
				gchar** _tmp23_;
				gchar** _tmp24_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp25_;
				gint _tmp25__length1;
				const gchar* _tmp26_;
				gchar** _tmp27_;
				gint _tmp27__length1;
				_tmp22_ = self->priv->keyring_command;
				_tmp24_ = _tmp23_ = lxsession_parse_command (_tmp22_, " ", 0);
				create_command = _tmp24_;
				create_command_length1 = _vala_array_length (_tmp23_);
				_create_command_size_ = create_command_length1;
				_tmp25_ = create_command;
				_tmp25__length1 = create_command_length1;
				_tmp26_ = _tmp25_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp26_);
				_tmp27_ = create_command;
				_tmp27__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp27_, _tmp27__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

static void
lxsession_keyring_app_class_init (LxsessionKeyringAppClass * klass,
                                  gpointer klass_data)
{
	lxsession_keyring_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionKeyringApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_keyring_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_keyring_app_finalize;
}

static void
lxsession_keyring_app_instance_init (LxsessionKeyringApp * self,
                                     gpointer klass)
{
	self->priv = lxsession_keyring_app_get_instance_private (self);
}

static void
lxsession_keyring_app_finalize (GObject * obj)
{
	LxsessionKeyringApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_KEYRING_APP, LxsessionKeyringApp);
	_g_free0 (self->priv->keyring_command);
	_g_free0 (self->priv->keyring_type);
	G_OBJECT_CLASS (lxsession_keyring_app_parent_class)->finalize (obj);
}

static GType
lxsession_keyring_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionKeyringAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_keyring_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionKeyringApp), 0, (GInstanceInitFunc) lxsession_keyring_app_instance_init, NULL };
	GType lxsession_keyring_app_type_id;
	lxsession_keyring_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionKeyringApp", &g_define_type_info, 0);
	LxsessionKeyringApp_private_offset = g_type_add_instance_private (lxsession_keyring_app_type_id, sizeof (LxsessionKeyringAppPrivate));
	return lxsession_keyring_app_type_id;
}

GType
lxsession_keyring_app_get_type (void)
{
	static volatile gsize lxsession_keyring_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_keyring_app_type_id__once)) {
		GType lxsession_keyring_app_type_id;
		lxsession_keyring_app_type_id = lxsession_keyring_app_get_type_once ();
		g_once_init_leave (&lxsession_keyring_app_type_id__once, lxsession_keyring_app_type_id);
	}
	return lxsession_keyring_app_type_id__once;
}

static inline gpointer
lxsession_screenshot_manager_app_get_instance_private (LxsessionScreenshotManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionScreenshotManagerApp_private_offset);
}

LxsessionScreenshotManagerApp*
lxsession_screenshot_manager_app_construct (GType object_type)
{
	LxsessionScreenshotManagerApp * self = NULL;
	self = (LxsessionScreenshotManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionScreenshotManagerApp*
lxsession_screenshot_manager_app_new (void)
{
	return lxsession_screenshot_manager_app_construct (LXSESSION_TYPE_SCREENSHOT_MANAGER_APP);
}

static void
lxsession_screenshot_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionScreenshotManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	self = (LxsessionScreenshotManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "screenshot_manager", "command");
	_g_free0 (self->priv->screenshotmanager_command);
	self->priv->screenshotmanager_command = _tmp1_;
	_tmp2_ = self->priv->screenshotmanager_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	switch (0) {
		default:
		{
			gchar** create_command = NULL;
			const gchar* _tmp6_;
			gchar** _tmp7_;
			gchar** _tmp8_;
			gint create_command_length1;
			gint _create_command_size_;
			const gchar* _tmp9_;
			_tmp6_ = self->priv->screenshotmanager_command;
			_tmp8_ = _tmp7_ = lxsession_parse_command (_tmp6_, " ", 0);
			create_command = _tmp8_;
			create_command_length1 = _vala_array_length (_tmp7_);
			_create_command_size_ = create_command_length1;
			_tmp9_ = create_command[0];
			lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp9_);
			lxsession_app_object_set_command ((LxsessionAppObject*) self, create_command, create_command_length1);
			create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
			break;
		}
	}
}

static gchar**
_vala_array_dup3 (gchar** self,
                  gssize length)
{
	if (length >= 0) {
		gchar** result;
		gssize i;
		result = g_new0 (gchar*, length + 1);
		for (i = 0; i < length; i++) {
			gchar* _tmp0_;
			_tmp0_ = g_strdup (self[i]);
			result[i] = _tmp0_;
		}
		return result;
	}
	return NULL;
}

void
lxsession_screenshot_manager_app_window_launch (LxsessionScreenshotManagerApp* self)
{
	gchar** backup_command = NULL;
	gchar** _tmp0_;
	gint _tmp0__length1;
	gint _tmp1_ = 0;
	gchar** _tmp2_;
	gint _tmp2__length1;
	gchar** _tmp3_;
	gint _tmp3__length1;
	gint backup_command_length1;
	gint _backup_command_size_;
	const gchar* _tmp4_;
	const gchar* _tmp5_;
	GQuark _tmp7_ = 0U;
	static GQuark _tmp6_label0 = 0;
	gchar** _tmp13_;
	gint _tmp13__length1;
	g_return_if_fail (self != NULL);
	lxsession_app_object_read_settings ((LxsessionAppObject*) self);
	_tmp0_ = lxsession_app_object_get_command ((LxsessionAppObject*) self, &_tmp1_);
	_tmp0__length1 = _tmp1_;
	_tmp2_ = _tmp0_;
	_tmp2__length1 = _tmp0__length1;
	_tmp3_ = (_tmp2_ != NULL) ? _vala_array_dup3 (_tmp2_, _tmp2__length1) : _tmp2_;
	_tmp3__length1 = _tmp2__length1;
	backup_command = _tmp3_;
	backup_command_length1 = _tmp3__length1;
	_backup_command_size_ = backup_command_length1;
	_tmp4_ = self->priv->screenshotmanager_command;
	_tmp5_ = _tmp4_;
	_tmp7_ = (NULL == _tmp5_) ? 0 : g_quark_from_string (_tmp5_);
	if (_tmp7_ == ((0 != _tmp6_label0) ? _tmp6_label0 : (_tmp6_label0 = g_quark_from_static_string ("scrot")))) {
		switch (0) {
			default:
			{
				gchar* create_window_command = NULL;
				gchar* _tmp8_;
				const gchar* _tmp9_;
				gchar** _tmp10_;
				gchar** _tmp11_;
				gchar** _tmp12_;
				gint _tmp12__length1;
				_tmp8_ = g_strdup ("scrot -u -b");
				create_window_command = _tmp8_;
				_tmp9_ = create_window_command;
				_tmp11_ = _tmp10_ = lxsession_parse_command (_tmp9_, " ", 0);
				_tmp12_ = _tmp11_;
				_tmp12__length1 = _vala_array_length (_tmp10_);
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp12_, _vala_array_length (_tmp10_));
				_tmp12_ = (_vala_array_free (_tmp12_, _tmp12__length1, (GDestroyNotify) g_free), NULL);
				_g_free0 (create_window_command);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				break;
			}
		}
	}
	lxsession_app_object_launch ((LxsessionAppObject*) self);
	_tmp13_ = backup_command;
	_tmp13__length1 = backup_command_length1;
	lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp13_, _tmp13__length1);
	backup_command = (_vala_array_free (backup_command, backup_command_length1, (GDestroyNotify) g_free), NULL);
}

static void
lxsession_screenshot_manager_app_class_init (LxsessionScreenshotManagerAppClass * klass,
                                             gpointer klass_data)
{
	lxsession_screenshot_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionScreenshotManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_screenshot_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_screenshot_manager_app_finalize;
}

static void
lxsession_screenshot_manager_app_instance_init (LxsessionScreenshotManagerApp * self,
                                                gpointer klass)
{
	self->priv = lxsession_screenshot_manager_app_get_instance_private (self);
}

static void
lxsession_screenshot_manager_app_finalize (GObject * obj)
{
	LxsessionScreenshotManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_SCREENSHOT_MANAGER_APP, LxsessionScreenshotManagerApp);
	_g_free0 (self->priv->screenshotmanager_command);
	G_OBJECT_CLASS (lxsession_screenshot_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_screenshot_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionScreenshotManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_screenshot_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionScreenshotManagerApp), 0, (GInstanceInitFunc) lxsession_screenshot_manager_app_instance_init, NULL };
	GType lxsession_screenshot_manager_app_type_id;
	lxsession_screenshot_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionScreenshotManagerApp", &g_define_type_info, 0);
	LxsessionScreenshotManagerApp_private_offset = g_type_add_instance_private (lxsession_screenshot_manager_app_type_id, sizeof (LxsessionScreenshotManagerAppPrivate));
	return lxsession_screenshot_manager_app_type_id;
}

GType
lxsession_screenshot_manager_app_get_type (void)
{
	static volatile gsize lxsession_screenshot_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_screenshot_manager_app_type_id__once)) {
		GType lxsession_screenshot_manager_app_type_id;
		lxsession_screenshot_manager_app_type_id = lxsession_screenshot_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_screenshot_manager_app_type_id__once, lxsession_screenshot_manager_app_type_id);
	}
	return lxsession_screenshot_manager_app_type_id__once;
}

static inline gpointer
lxsession_updates_manager_app_get_instance_private (LxsessionUpdatesManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionUpdatesManagerApp_private_offset);
}

LxsessionUpdatesManagerApp*
lxsession_updates_manager_app_construct (GType object_type)
{
	LxsessionUpdatesManagerApp * self = NULL;
	self = (LxsessionUpdatesManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionUpdatesManagerApp*
lxsession_updates_manager_app_new (void)
{
	return lxsession_updates_manager_app_construct (LXSESSION_TYPE_UPDATES_MANAGER_APP);
}

static void
lxsession_updates_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionUpdatesManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	static GQuark _tmp4_label0 = 0;
	static GQuark _tmp4_label1 = 0;
	static GQuark _tmp4_label2 = 0;
	static GQuark _tmp4_label3 = 0;
	self = (LxsessionUpdatesManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "updates_manager", "command");
	_g_free0 (self->priv->updatesmanager_command);
	self->priv->updatesmanager_command = _tmp1_;
	_tmp2_ = self->priv->updatesmanager_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	if (_tmp5_ == ((0 != _tmp4_label0) ? _tmp4_label0 : (_tmp4_label0 = g_quark_from_static_string (NULL)))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label1) ? _tmp4_label1 : (_tmp4_label1 = g_quark_from_static_string ("")))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label2) ? _tmp4_label2 : (_tmp4_label2 = g_quark_from_static_string (" ")))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label3) ? _tmp4_label3 : (_tmp4_label3 = g_quark_from_static_string ("update-notifier-clone")))) {
		switch (0) {
			default:
			{
				lxsession_updates_manager_app_setup_apt_config (self);
				lxsession_updates_manager_app_setup_reboot_config (self);
				lxsession_updates_manager_app_run_check (self);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp6_;
				gchar** _tmp7_;
				gchar** _tmp8_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp9_;
				gint _tmp9__length1;
				const gchar* _tmp10_;
				gchar** _tmp11_;
				gint _tmp11__length1;
				_tmp6_ = self->priv->updatesmanager_command;
				_tmp8_ = _tmp7_ = lxsession_parse_command (_tmp6_, " ", 0);
				create_command = _tmp8_;
				create_command_length1 = _vala_array_length (_tmp7_);
				_create_command_size_ = create_command_length1;
				_tmp9_ = create_command;
				_tmp9__length1 = create_command_length1;
				_tmp10_ = _tmp9_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp10_);
				_tmp11_ = create_command;
				_tmp11__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp11_, _tmp11__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

void
lxsession_updates_manager_app_test_inactivate (LxsessionUpdatesManagerApp* self)
{
	LxsessionIconObject* _tmp0_;
	g_return_if_fail (self != NULL);
	_tmp0_ = self->priv->updates_icon;
	lxsession_icon_object_inactivate (_tmp0_);
}

void
lxsession_updates_manager_app_test_activate (LxsessionUpdatesManagerApp* self)
{
	LxsessionIconObject* _tmp0_;
	g_return_if_fail (self != NULL);
	_tmp0_ = self->priv->updates_icon;
	lxsession_icon_object_activate (_tmp0_);
}

static gboolean
_lxsession_updates_manager_app_on_apt_update_file_change_gsource_func (gpointer self)
{
	gboolean result;
	result = lxsession_updates_manager_app_on_apt_update_file_change ((LxsessionUpdatesManagerApp*) self);
	return result;
}

void
lxsession_updates_manager_app_run_check (LxsessionUpdatesManagerApp* self)
{
	g_return_if_fail (self != NULL);
	if (self->priv->lock_check == 0) {
		const gchar* _tmp0_;
		_tmp0_ = self->priv->dpkg_lock_file;
		if (lxsession_updates_manager_app_check_lock_file (self, _tmp0_) == TRUE) {
			gint timeout = 0;
			LxsessionLxsessionConfigKeyFile* _tmp1_;
			gchar* _tmp2_;
			gchar* _tmp3_;
			gboolean _tmp4_;
			self->priv->lock_check = 1;
			timeout = 60;
			_tmp1_ = lxsession_global_settings;
			_tmp2_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp1_, "Session", "updates_manager", "timeout");
			_tmp3_ = _tmp2_;
			_tmp4_ = _tmp3_ != NULL;
			_g_free0 (_tmp3_);
			if (_tmp4_) {
				LxsessionLxsessionConfigKeyFile* _tmp5_;
				gchar* _tmp6_;
				gchar* _tmp7_;
				_tmp5_ = lxsession_global_settings;
				_tmp6_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp5_, "Session", "updates_manager", "timeout");
				_tmp7_ = _tmp6_;
				if (!lxsession_parse_timeout (_tmp7_, &timeout))
					timeout = 60;
				_g_free0 (_tmp7_);
			}
			g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, (guint) timeout, _lxsession_updates_manager_app_on_apt_update_file_change_gsource_func, g_object_ref (self), g_object_unref);
		}
	}
}

void
lxsession_updates_manager_app_reboot_launch (LxsessionUpdatesManagerApp* self)
{
	LxsessionSessionObject* session = NULL;
	LxsessionSessionObject* _tmp0_;
	g_return_if_fail (self != NULL);
	_tmp0_ = lxsession_session_object_new ();
	session = _tmp0_;
	lxsession_session_object_luxesession_restart (session);
	_g_object_unref0 (session);
}

static void
____lambda11_ (LxsessionUpdatesManagerApp* self)
{
	lxsession_updates_manager_app_reboot_launch (self);
}

static void
_____lambda11__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                    gpointer self)
{
	____lambda11_ ((LxsessionUpdatesManagerApp*) self);
}

static void
____lambda12_ (LxsessionUpdatesManagerApp* self)
{
	lxsession_updates_manager_app_reboot_launch (self);
}

static void
_____lambda12__lxsession_icon_object_action_callback (gpointer self)
{
	____lambda12_ ((LxsessionUpdatesManagerApp*) self);
}

void
lxsession_updates_manager_app_run_check_reboot (LxsessionUpdatesManagerApp* self)
{
	gchar* notification_text = NULL;
	gchar* _tmp0_;
	GFile* _tmp1_;
	GError* _inner_error0_ = NULL;
	g_return_if_fail (self != NULL);
	_tmp0_ = g_strdup ("");
	notification_text = _tmp0_;
	_tmp1_ = self->priv->reboot_file;
	if (g_file_query_exists (_tmp1_, NULL)) {
		LxsessionIconObject* _tmp2_;
		_tmp2_ = self->priv->reboot_icon;
		if (_tmp2_ == NULL) {
			LxsessionMenuObject* reboot_icon_menu = NULL;
			LxsessionMenuObject* _tmp3_;
			gchar* line = NULL;
			gchar* _tmp4_;
			const gchar* _tmp15_;
			LxsessionMenuItemObject* menu_item = NULL;
			LxsessionMenuItemObject* _tmp18_;
			LxsessionMenuItemObject* _tmp19_;
			const gchar* _tmp20_;
			LxsessionMenuItemObject* _tmp21_;
			LxsessionMenuItemObject* _tmp22_;
			LxsessionMenuObject* _tmp23_;
			LxsessionMenuItemObject* _tmp24_;
			const gchar* _tmp25_;
			LxsessionMenuObject* _tmp26_;
			LxsessionIconObject* _tmp27_;
			LxsessionIconObject* _tmp28_;
			LxsessionIconObject* _tmp29_;
			LxsessionIconObject* _tmp30_;
			LxsessionIconObject* _tmp31_;
			_tmp3_ = lxsession_menu_object_new ();
			reboot_icon_menu = _tmp3_;
			_tmp4_ = g_strdup (_ ("Reboot required"));
			line = _tmp4_;
			{
				GFileInputStream* _tmp5_ = NULL;
				GFile* _tmp6_;
				GFileInputStream* _tmp7_;
				GDataInputStream* dis = NULL;
				GDataInputStream* _tmp8_;
				gchar* _tmp9_ = NULL;
				GDataInputStream* _tmp10_;
				gchar* _tmp11_;
				gchar* _tmp12_;
				_tmp6_ = self->priv->reboot_file;
				_tmp7_ = g_file_read (_tmp6_, NULL, &_inner_error0_);
				_tmp5_ = _tmp7_;
				if (G_UNLIKELY (_inner_error0_ != NULL)) {
					goto __catch0_g_error;
				}
				_tmp8_ = g_data_input_stream_new ((GInputStream*) _tmp5_);
				dis = _tmp8_;
				_tmp10_ = dis;
				_tmp11_ = g_data_input_stream_read_line (_tmp10_, NULL, NULL, &_inner_error0_);
				_tmp9_ = _tmp11_;
				if (G_UNLIKELY (_inner_error0_ != NULL)) {
					_g_object_unref0 (dis);
					_g_object_unref0 (_tmp5_);
					goto __catch0_g_error;
				}
				_tmp12_ = _tmp9_;
				_tmp9_ = NULL;
				_g_free0 (line);
				line = _tmp12_;
				_g_free0 (_tmp9_);
				_g_object_unref0 (dis);
				_g_object_unref0 (_tmp5_);
			}
			goto __finally0;
			__catch0_g_error:
			{
				GError* e = NULL;
				GError* _tmp13_;
				const gchar* _tmp14_;
				e = _inner_error0_;
				_inner_error0_ = NULL;
				_tmp13_ = e;
				_tmp14_ = _tmp13_->message;
				g_message ("app.vala:1282: Error: %s\n", _tmp14_);
				_g_error_free0 (e);
			}
			__finally0:
			if (G_UNLIKELY (_inner_error0_ != NULL)) {
				_g_free0 (line);
				_g_object_unref0 (reboot_icon_menu);
				_g_free0 (notification_text);
				g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
				g_clear_error (&_inner_error0_);
				return;
			}
			_tmp15_ = line;
			if (_tmp15_ != NULL) {
				const gchar* _tmp16_;
				gchar* _tmp17_;
				_tmp16_ = line;
				_tmp17_ = g_strdup (_tmp16_);
				_g_free0 (notification_text);
				notification_text = _tmp17_;
			}
			_tmp18_ = lxsession_menu_item_object_new ();
			menu_item = _tmp18_;
			_tmp19_ = menu_item;
			_tmp20_ = notification_text;
			lxsession_menu_item_object_set_label (_tmp19_, _tmp20_);
			_tmp21_ = menu_item;
			g_signal_connect_object (_tmp21_, "activate", (GCallback) _____lambda11__lxsession_menu_item_object_activate, self, 0);
			_tmp22_ = menu_item;
			lxsession_menu_item_object_show (_tmp22_);
			_tmp23_ = reboot_icon_menu;
			_tmp24_ = menu_item;
			lxsession_menu_object_add (_tmp23_, (GObject*) _tmp24_);
			_tmp25_ = notification_text;
			_tmp26_ = reboot_icon_menu;
			_tmp27_ = lxsession_icon_object_new ("RebootIcon", "system-reboot", _tmp25_, _tmp26_);
			_g_object_unref0 (self->priv->reboot_icon);
			self->priv->reboot_icon = _tmp27_;
			_tmp28_ = self->priv->reboot_icon;
			lxsession_icon_object_init (_tmp28_);
			_tmp29_ = self->priv->reboot_icon;
			lxsession_icon_object_clear_actions (_tmp29_);
			_tmp30_ = self->priv->reboot_icon;
			lxsession_icon_object_add_action (_tmp30_, "launch_reboot", _ ("Reboot"), _____lambda12__lxsession_icon_object_action_callback, g_object_ref (self), g_object_unref);
			_tmp31_ = self->priv->reboot_icon;
			lxsession_icon_object_activate (_tmp31_);
			_g_object_unref0 (menu_item);
			_g_free0 (line);
			_g_object_unref0 (reboot_icon_menu);
		}
	} else {
		LxsessionIconObject* _tmp32_;
		_tmp32_ = self->priv->reboot_icon;
		if (_tmp32_ != NULL) {
			LxsessionIconObject* _tmp33_;
			_tmp33_ = self->priv->reboot_icon;
			lxsession_icon_object_inactivate (_tmp33_);
		}
	}
	_g_free0 (notification_text);
}

void
lxsession_updates_manager_app_language_launch (LxsessionUpdatesManagerApp* self)
{
	LxsessionIconObject* _tmp0_;
	GError* _inner_error0_ = NULL;
	g_return_if_fail (self != NULL);
	_tmp0_ = self->priv->language_icon;
	if (_tmp0_ != NULL) {
		{
			LxsessionIconObject* _tmp1_;
			lxsession_luxesession_spawn_command_line_async("gnome-language-selector");
			if (G_UNLIKELY (_inner_error0_ != NULL)) {
				if (_inner_error0_->domain == G_SPAWN_ERROR) {
					goto __catch0_g_spawn_error;
				}
				g_critical ("file %s: line %d: unexpected error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
				g_clear_error (&_inner_error0_);
				return;
			}
			_tmp1_ = self->priv->language_icon;
			lxsession_icon_object_inactivate (_tmp1_);
		}
		goto __finally0;
		__catch0_g_spawn_error:
		{
			GError* err = NULL;
			GError* _tmp2_;
			const gchar* _tmp3_;
			err = _inner_error0_;
			_inner_error0_ = NULL;
			_tmp2_ = err;
			_tmp3_ = _tmp2_->message;
			g_warning ("app.vala:1328: %s", _tmp3_);
			_g_error_free0 (err);
		}
		__finally0:
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
			g_clear_error (&_inner_error0_);
			return;
		}
	}
}

static void
_____lambda9_ (LxsessionUpdatesManagerApp* self)
{
	lxsession_updates_manager_app_language_launch (self);
}

static void
______lambda9__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                    gpointer self)
{
	_____lambda9_ ((LxsessionUpdatesManagerApp*) self);
}

static void
_____lambda10_ (LxsessionUpdatesManagerApp* self)
{
	lxsession_updates_manager_app_language_launch (self);
}

static void
______lambda10__lxsession_icon_object_action_callback (gpointer self)
{
	_____lambda10_ ((LxsessionUpdatesManagerApp*) self);
}

static gboolean
_lxsession_updates_manager_app_check_language_support_sync_gsource_func (gpointer self)
{
	gboolean result;
	result = lxsession_updates_manager_app_check_language_support_sync ((LxsessionUpdatesManagerApp*) self);
	return result;
}

void
lxsession_updates_manager_app_check_language_support (LxsessionUpdatesManagerApp* self)
{
	/* Mirror of UpdatesManagerApp.check_language_support in app.vala:
	 * check-language-support can block, so run it on idle instead of
	 * the startup critical path. */
	g_return_if_fail (self != NULL);
	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, _lxsession_updates_manager_app_check_language_support_sync_gsource_func, g_object_ref (self), g_object_unref);
}

static gboolean
lxsession_updates_manager_app_check_language_support_sync (LxsessionUpdatesManagerApp* self)
{
	gchar* command = NULL;
	gchar* _tmp0_;
	gchar* standard_output = NULL;
	gchar* standard_error = NULL;
	gint exit_status = 0;
	gchar* launch_string = NULL;
	gchar* _tmp1_;
	GError* _inner_error0_ = NULL;
	g_return_val_if_fail (self != NULL, FALSE);
	_tmp0_ = g_strdup ("check-language-support");
	command = _tmp0_;
	_tmp1_ = g_strdup (_ ("Launch language support"));
	launch_string = _tmp1_;
	{
		gchar* _tmp2_ = NULL;
		gchar* _tmp3_ = NULL;
		gint _tmp4_ = 0;
		g_spawn_command_line_sync (command, &_tmp2_, &_tmp3_, &_tmp4_, &_inner_error0_);
		_g_free0 (standard_output);
		standard_output = _tmp2_;
		_g_free0 (standard_error);
		standard_error = _tmp3_;
		exit_status = _tmp4_;
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			if (_inner_error0_->domain == G_SPAWN_ERROR) {
				goto __catch0_g_spawn_error;
			}
			_g_free0 (launch_string);
			_g_free0 (standard_error);
			_g_free0 (standard_output);
			_g_free0 (command);
			g_critical ("file %s: line %d: unexpected error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
			g_clear_error (&_inner_error0_);
			return FALSE;
		}
		g_message ("app.vala:1347: Launching %s", command);
		g_message ("app.vala:1348: Language state: %s", standard_error);
		g_message ("app.vala:1349: Language exit status: %i", exit_status);
		g_message ("app.vala:1350: Language output: %s", standard_output);
	}
	goto __finally0;
	__catch0_g_spawn_error:
	{
		GError* err = NULL;
		GError* _tmp5_;
		const gchar* _tmp6_;
		err = _inner_error0_;
		_inner_error0_ = NULL;
		_tmp5_ = err;
		_tmp6_ = _tmp5_->message;
		g_warning ("app.vala:1355: %s", _tmp6_);
		_g_error_free0 (err);
	}
	__finally0:
	if (G_UNLIKELY (_inner_error0_ != NULL)) {
		_g_free0 (launch_string);
		_g_free0 (standard_error);
		_g_free0 (standard_output);
		_g_free0 (command);
		g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
		g_clear_error (&_inner_error0_);
		return FALSE;
	}
	if (standard_output != NULL) {
		gint _tmp7_;
		gint _tmp8_;
		_tmp7_ = strlen (standard_output);
		_tmp8_ = _tmp7_;
		if (_tmp8_ >= 3) {
			LxsessionIconObject* _tmp9_;
			_tmp9_ = self->priv->language_icon;
			if (_tmp9_ == NULL) {
				LxsessionMenuObject* language_icon_menu = NULL;
				LxsessionMenuObject* _tmp10_;
				LxsessionMenuItemObject* menu_item = NULL;
				LxsessionMenuItemObject* _tmp11_;
				LxsessionMenuItemObject* _tmp12_;
				LxsessionMenuItemObject* _tmp13_;
				LxsessionMenuItemObject* _tmp14_;
				LxsessionMenuObject* _tmp15_;
				LxsessionMenuItemObject* _tmp16_;
				LxsessionMenuObject* _tmp17_;
				LxsessionIconObject* _tmp18_;
				LxsessionIconObject* _tmp19_;
				LxsessionIconObject* _tmp20_;
				LxsessionIconObject* _tmp21_;
				LxsessionIconObject* _tmp22_;
				_tmp10_ = lxsession_menu_object_new ();
				language_icon_menu = _tmp10_;
				_tmp11_ = lxsession_menu_item_object_new ();
				menu_item = _tmp11_;
				_tmp12_ = menu_item;
				lxsession_menu_item_object_set_label (_tmp12_, launch_string);
				_tmp13_ = menu_item;
				g_signal_connect_object (_tmp13_, "activate", (GCallback) ______lambda9__lxsession_menu_item_object_activate, self, 0);
				_tmp14_ = menu_item;
				lxsession_menu_item_object_show (_tmp14_);
				_tmp15_ = language_icon_menu;
				_tmp16_ = menu_item;
				lxsession_menu_object_add (_tmp15_, (GObject*) _tmp16_);
				_tmp17_ = language_icon_menu;
				_tmp18_ = lxsession_icon_object_new ("LanguageIcon", "preferences-desktop-locale", _ ("Language support missing"), _tmp17_);
				_g_object_unref0 (self->priv->language_icon);
				self->priv->language_icon = _tmp18_;
				_tmp19_ = self->priv->language_icon;
				lxsession_icon_object_init (_tmp19_);
				_tmp20_ = self->priv->language_icon;
				lxsession_icon_object_clear_actions (_tmp20_);
				_tmp21_ = self->priv->language_icon;
				lxsession_icon_object_add_action (_tmp21_, "launch_language_support", launch_string, ______lambda10__lxsession_icon_object_action_callback, g_object_ref (self), g_object_unref);
				_tmp22_ = self->priv->language_icon;
				lxsession_icon_object_activate (_tmp22_);
				_g_object_unref0 (menu_item);
				_g_object_unref0 (language_icon_menu);
			}
		} else {
			LxsessionIconObject* _tmp23_;
			_tmp23_ = self->priv->language_icon;
			if (_tmp23_ != NULL) {
				LxsessionIconObject* _tmp24_;
				_tmp24_ = self->priv->language_icon;
				lxsession_icon_object_inactivate (_tmp24_);
			}
		}
	} else {
		LxsessionIconObject* _tmp25_;
		_tmp25_ = self->priv->language_icon;
		if (_tmp25_ != NULL) {
			LxsessionIconObject* _tmp26_;
			_tmp26_ = self->priv->language_icon;
			lxsession_icon_object_inactivate (_tmp26_);
		}
	}
	_g_free0 (launch_string);
	_g_free0 (standard_error);
	_g_free0 (standard_output);
	_g_free0 (command);
	return FALSE;
}

void
lxsession_updates_manager_app_upgrade_launch (LxsessionUpdatesManagerApp* self,
                                              const gchar* upgrade_manager_command)
{
	GError* _inner_error0_ = NULL;
	g_return_if_fail (self != NULL);
	g_return_if_fail (upgrade_manager_command != NULL);
	{
		lxsession_luxesession_spawn_command_line_async(upgrade_manager_command);
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			if (_inner_error0_->domain == G_SPAWN_ERROR) {
				goto __catch0_g_spawn_error;
			}
			g_critical ("file %s: line %d: unexpected error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
			g_clear_error (&_inner_error0_);
			return;
		}
	}
	goto __finally0;
	__catch0_g_spawn_error:
	{
		GError* err = NULL;
		GError* _tmp0_;
		const gchar* _tmp1_;
		err = _inner_error0_;
		_inner_error0_ = NULL;
		_tmp0_ = err;
		_tmp1_ = _tmp0_->message;
		g_warning ("app.vala:1403: %s", _tmp1_);
		_g_error_free0 (err);
	}
	__finally0:
	if (G_UNLIKELY (_inner_error0_ != NULL)) {
		g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
		g_clear_error (&_inner_error0_);
		return;
	}
}

static Block1Data*
block1_data_ref (Block1Data* _data1_)
{
	g_atomic_int_inc (&_data1_->_ref_count_);
	return _data1_;
}

static void
block1_data_unref (void * _userdata_)
{
	Block1Data* _data1_;
	_data1_ = (Block1Data*) _userdata_;
	if (g_atomic_int_dec_and_test (&_data1_->_ref_count_)) {
		LxsessionUpdatesManagerApp* self;
		self = _data1_->self;
		_g_free0 (_data1_->upgrade_manager_command);
		_g_object_unref0 (self);
		g_slice_free (Block1Data, _data1_);
	}
}

static void
____lambda5_ (Block1Data* _data1_)
{
	LxsessionUpdatesManagerApp* self;
	const gchar* _tmp0_;
	self = _data1_->self;
	_tmp0_ = _data1_->upgrade_manager_command;
	lxsession_updates_manager_app_upgrade_launch (self, _tmp0_);
}

static void
_____lambda5__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                   gpointer self)
{
	____lambda5_ (self);
}

static void
____lambda6_ (Block1Data* _data1_)
{
	LxsessionUpdatesManagerApp* self;
	const gchar* _tmp0_;
	self = _data1_->self;
	_tmp0_ = _data1_->upgrade_manager_command;
	lxsession_updates_manager_app_upgrade_launch (self, _tmp0_);
}

static void
_____lambda6__lxsession_icon_object_action_callback (gpointer self)
{
	____lambda6_ (self);
}

static gchar*
string_slice (const gchar* self,
              glong start,
              glong end)
{
	glong string_length = 0L;
	gint _tmp0_;
	gint _tmp1_;
	gboolean _tmp2_ = FALSE;
	gboolean _tmp3_ = FALSE;
	gchar* _tmp4_;
	gchar* result;
	g_return_val_if_fail (self != NULL, NULL);
	_tmp0_ = strlen (self);
	_tmp1_ = _tmp0_;
	string_length = (glong) _tmp1_;
	if (start < ((glong) 0)) {
		start = string_length + start;
	}
	if (end < ((glong) 0)) {
		end = string_length + end;
	}
	if (start >= ((glong) 0)) {
		_tmp2_ = start <= string_length;
	} else {
		_tmp2_ = FALSE;
	}
	g_return_val_if_fail (_tmp2_, NULL);
	if (end >= ((glong) 0)) {
		_tmp3_ = end <= string_length;
	} else {
		_tmp3_ = FALSE;
	}
	g_return_val_if_fail (_tmp3_, NULL);
	g_return_val_if_fail (start <= end, NULL);
	_tmp4_ = g_strndup (((gchar*) self) + start, (gsize) (end - start));
	result = _tmp4_;
	return result;
}

static void
___lambda7_ (LxsessionUpdatesManagerApp* self)
{
	lxsession_updates_manager_app_upgrade_launch (self, "synaptic-pkexec");
}

static void
____lambda7__lxsession_icon_object_action_callback (gpointer self)
{
	___lambda7_ ((LxsessionUpdatesManagerApp*) self);
}

static void
___lambda8_ (LxsessionUpdatesManagerApp* self)
{
	lxsession_updates_manager_app_upgrade_launch (self, "synaptic-pkexec");
}

static void
____lambda8__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                  gpointer self)
{
	___lambda8_ ((LxsessionUpdatesManagerApp*) self);
}

static void
lxsession_apt_check_communicated (GObject *source, GAsyncResult *result,
				 gpointer user_data)
{
	LxsessionUpdatesManagerApp *self = user_data;
	GSubprocess *process = G_SUBPROCESS (source);
	GError *error = NULL;
	gchar *stdout_text = NULL, *stderr_text = NULL;
	if (!g_subprocess_communicate_utf8_finish (process, result, &stdout_text,
						  &stderr_text, &error)) {
		g_clear_error (&error);
		stderr_text = g_strdup ("Update check failed before producing a result");
		stdout_text = g_strdup ("");
	}
	self->priv->apt_check_stdout = stdout_text ? stdout_text : g_strdup ("");
	self->priv->apt_check_stderr = stderr_text ? stderr_text : g_strdup ("");
	self->priv->apt_check_exit_status = g_subprocess_get_if_exited (process)
		? g_subprocess_get_exit_status (process) : -1;
	self->priv->apt_check_pending = FALSE;
	self->priv->apt_check_result_ready = TRUE;
	lxsession_updates_manager_app_on_apt_update_file_change (self);
	g_object_unref (self);
}

gboolean
lxsession_updates_manager_app_on_apt_update_file_change (LxsessionUpdatesManagerApp* self)
{
	Block1Data* _data1_;
	gchar* standard_output = NULL;
	gchar* standard_error = NULL;
	gint exit_status = 0;
	gchar* notification_text = NULL;
	gchar* _tmp0_;
	gchar* launch_string = NULL;
	gchar* _tmp1_;
	gchar* _tmp2_;
	gchar** updates_num = NULL;
	gint updates_num_length1 = 0;
	gint _updates_num_size_ = 0;
	gint updates_urgent = 0;
	gint updates_normal = 0;
	gint updates_state = 0;
	gint number_updates = 0;
	LxsessionMenuObject* _tmp3_;
	gchar* command = NULL;
	gchar* _tmp18_;
	gchar* error_string = NULL;
	gchar* _tmp19_;
	LxsessionIconObject* _tmp27_;
	GError* _inner_error0_ = NULL;
	gboolean result;
	g_return_val_if_fail (self != NULL, FALSE);
	_data1_ = g_slice_new0 (Block1Data);
	_data1_->_ref_count_ = 1;
	_data1_->self = g_object_ref (self);
	_tmp0_ = g_strdup (_ ("Updates available"));
	notification_text = _tmp0_;
	_tmp1_ = g_strdup (_ ("Launch Update Manager"));
	launch_string = _tmp1_;
	_tmp2_ = g_strdup ("");
	_data1_->upgrade_manager_command = _tmp2_;
	updates_urgent = 0;
	updates_normal = 0;
	updates_state = 0;
	number_updates = 0;
	self->priv->lock_check = 1;
	_tmp3_ = self->priv->icon_menu;
	if (_tmp3_ == NULL) {
		LxsessionMenuObject* _tmp4_;
		LxsessionLxsessionConfigKeyFile* _tmp5_;
		gchar* _tmp6_;
		gchar* _tmp7_;
		gboolean _tmp8_;
		_tmp4_ = lxsession_menu_object_new ();
		_g_object_unref0 (self->priv->icon_menu);
		self->priv->icon_menu = _tmp4_;
		_tmp5_ = lxsession_global_settings;
		_tmp6_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp5_, "Session", "upgrade_manager", "command");
		_tmp7_ = _tmp6_;
		_tmp8_ = _tmp7_ != NULL;
		_g_free0 (_tmp7_);
		if (_tmp8_) {
			LxsessionLxsessionConfigKeyFile* _tmp9_;
			gchar* _tmp10_;
			LxsessionMenuItemObject* menu_item = NULL;
			LxsessionMenuItemObject* _tmp11_;
			LxsessionMenuItemObject* _tmp12_;
			const gchar* _tmp13_;
			LxsessionMenuItemObject* _tmp14_;
			LxsessionMenuItemObject* _tmp15_;
			LxsessionMenuObject* _tmp16_;
			LxsessionMenuItemObject* _tmp17_;
			_tmp9_ = lxsession_global_settings;
			_tmp10_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp9_, "Session", "upgrade_manager", "command");
			_g_free0 (_data1_->upgrade_manager_command);
			_data1_->upgrade_manager_command = _tmp10_;
			_tmp11_ = lxsession_menu_item_object_new ();
			menu_item = _tmp11_;
			_tmp12_ = menu_item;
			_tmp13_ = launch_string;
			lxsession_menu_item_object_set_label (_tmp12_, _tmp13_);
			_tmp14_ = menu_item;
			g_signal_connect_data (_tmp14_, "activate", (GCallback) _____lambda5__lxsession_menu_item_object_activate, block1_data_ref (_data1_), (GClosureNotify) block1_data_unref, 0);
			_tmp15_ = menu_item;
			lxsession_menu_item_object_show (_tmp15_);
			_tmp16_ = self->priv->icon_menu;
			_tmp17_ = menu_item;
			lxsession_menu_object_add (_tmp16_, (GObject*) _tmp17_);
			_g_object_unref0 (menu_item);
		}
	}
	_tmp18_ = g_strdup ("/usr/bin/timeout 30s /usr/bin/nice /usr/bin/ionice -c3 /usr/lib/update-notifier/apt-check");
	command = _tmp18_;
	_tmp19_ = g_strdup ("");
	error_string = _tmp19_;
	if (!self->priv->apt_check_result_ready) {
		if (!self->priv->apt_check_pending) {
			static const gchar *argv[] = {
				"/usr/bin/timeout", "30s", "/usr/bin/nice", "/usr/bin/ionice",
				"-c3", "/usr/lib/update-notifier/apt-check", NULL
			};
			GError *spawn_error = NULL;
			GSubprocess *process = g_subprocess_newv (argv,
				G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
				&spawn_error);
			self->priv->apt_check_pending = TRUE;
			if (process != NULL) {
				g_subprocess_communicate_utf8_async (process, NULL, NULL,
					(GAsyncReadyCallback) lxsession_apt_check_communicated,
					g_object_ref (self));
				g_object_unref (process);
			} else {
				self->priv->apt_check_pending = FALSE;
				self->priv->apt_check_result_ready = TRUE;
				self->priv->apt_check_stderr = g_strdup (spawn_error->message);
				self->priv->apt_check_stdout = g_strdup ("");
				self->priv->apt_check_exit_status = -1;
				g_clear_error (&spawn_error);
				lxsession_updates_manager_app_on_apt_update_file_change (self);
			}
		}
		_g_free0 (error_string);
		_g_free0 (command);
		updates_num = (_vala_array_free (updates_num, updates_num_length1, (GDestroyNotify) g_free), NULL);
		_g_free0 (launch_string);
		_g_free0 (notification_text);
		_g_free0 (standard_error);
		_g_free0 (standard_output);
		block1_data_unref (_data1_);
		return FALSE;
	}
	standard_output = g_steal_pointer (&self->priv->apt_check_stdout);
	standard_error = g_steal_pointer (&self->priv->apt_check_stderr);
	exit_status = self->priv->apt_check_exit_status;
	self->priv->apt_check_result_ready = FALSE;
	g_message ("app.vala:1456: Update check completed (%i)", exit_status);
	goto __finally0;
	__catch0_g_spawn_error:
	{
		GError* err = NULL;
		GError* _tmp24_;
		const gchar* _tmp25_;
		err = _inner_error0_;
		_inner_error0_ = NULL;
		_tmp24_ = err;
		_tmp25_ = _tmp24_->message;
		g_warning ("app.vala:1463: %s", _tmp25_);
		_g_error_free0 (err);
	}
	__finally0:
	if (G_UNLIKELY (_inner_error0_ != NULL)) {
		gboolean _tmp26_ = FALSE;
		_g_free0 (error_string);
		_g_free0 (command);
		updates_num = (_vala_array_free (updates_num, updates_num_length1, (GDestroyNotify) g_free), NULL);
		_g_free0 (launch_string);
		_g_free0 (notification_text);
		_g_free0 (standard_error);
		_g_free0 (standard_output);
		block1_data_unref (_data1_);
		_data1_ = NULL;
		g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
		g_clear_error (&_inner_error0_);
		return _tmp26_;
	}
	_tmp27_ = self->priv->updates_icon;
	if (_tmp27_ == NULL) {
		const gchar* _tmp28_;
		LxsessionMenuObject* _tmp29_;
		LxsessionIconObject* _tmp30_;
		LxsessionIconObject* _tmp31_;
		LxsessionLxsessionConfigKeyFile* _tmp32_;
		gchar* _tmp33_;
		gchar* _tmp34_;
		gboolean _tmp35_;
		LxsessionIconObject* _tmp41_;
		_tmp28_ = notification_text;
		_tmp29_ = self->priv->icon_menu;
		_tmp30_ = lxsession_icon_object_new ("UpdatesIcon", "software-update-available", _tmp28_, _tmp29_);
		_g_object_unref0 (self->priv->updates_icon);
		self->priv->updates_icon = _tmp30_;
		_tmp31_ = self->priv->updates_icon;
		lxsession_icon_object_init (_tmp31_);
		_tmp32_ = lxsession_global_settings;
		_tmp33_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp32_, "Session", "upgrade_manager", "command");
		_tmp34_ = _tmp33_;
		_tmp35_ = _tmp34_ != NULL;
		_g_free0 (_tmp34_);
		if (_tmp35_) {
			LxsessionLxsessionConfigKeyFile* _tmp36_;
			gchar* _tmp37_;
			LxsessionIconObject* _tmp38_;
			LxsessionIconObject* _tmp39_;
			const gchar* _tmp40_;
			_tmp36_ = lxsession_global_settings;
			_tmp37_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp36_, "Session", "upgrade_manager", "command");
			_g_free0 (_data1_->upgrade_manager_command);
			_data1_->upgrade_manager_command = _tmp37_;
			_tmp38_ = self->priv->updates_icon;
			lxsession_icon_object_clear_actions (_tmp38_);
			_tmp39_ = self->priv->updates_icon;
			_tmp40_ = launch_string;
			lxsession_icon_object_add_action (_tmp39_, "launch_upgrade_manager", _tmp40_, _____lambda6__lxsession_icon_object_action_callback, block1_data_ref (_data1_), block1_data_unref);
		}
		_tmp41_ = self->priv->updates_icon;
		lxsession_icon_object_inactivate (_tmp41_);
	} else {
		LxsessionIconObject* _tmp42_;
		_tmp42_ = self->priv->updates_icon;
		lxsession_icon_object_inactivate (_tmp42_);
	}
	if (g_strcmp0 (standard_error, "") != 0) {
		gchar* _tmp43_;
		gchar* _tmp44_;
		gboolean _tmp45_;
		_tmp43_ = string_slice (standard_error, (glong) 0, (glong) 1);
		_tmp44_ = _tmp43_;
		_tmp45_ = g_strcmp0 (_tmp44_, "E") == 0;
		_g_free0 (_tmp44_);
		if (_tmp45_) {
			gchar* _tmp46_;
			gint _tmp47_;
			gint _tmp48_;
			updates_urgent = 0;
			updates_normal = 0;
			updates_state = 1;
			_tmp46_ = g_strdup (_ ("An error occurred, please run Package Manager from the left-click menu" \
" or apt-get in a terminal to see what is wrong."));
			_g_free0 (error_string);
			error_string = _tmp46_;
			_tmp47_ = strlen (standard_error);
			_tmp48_ = _tmp47_;
			if (_tmp48_ > 3) {
				const gchar* _tmp49_;
				gchar* _tmp50_;
				gchar* _tmp51_;
				gchar* _tmp52_;
				gchar* _tmp53_;
				gchar* _tmp54_;
				_tmp49_ = error_string;
				_tmp50_ = g_strconcat (_tmp49_, "\n", NULL);
				_tmp51_ = _tmp50_;
				_tmp52_ = g_strconcat (_tmp51_, _ ("The error message was: "), NULL);
				_tmp53_ = _tmp52_;
				_tmp54_ = g_strconcat (_tmp53_, standard_error, NULL);
				_g_free0 (notification_text);
				notification_text = _tmp54_;
				_g_free0 (_tmp53_);
				_g_free0 (_tmp51_);
			} else {
				const gchar* _tmp55_;
				gchar* _tmp56_;
				_tmp55_ = error_string;
				_tmp56_ = g_strdup (_tmp55_);
				_g_free0 (notification_text);
				notification_text = _tmp56_;
			}
		} else {
			gchar** _tmp57_;
			gchar** _tmp58_;
			gchar** _tmp59_;
			gint _tmp59__length1;
			const gchar* _tmp60_;
			gchar** _tmp61_;
			gint _tmp61__length1;
			const gchar* _tmp62_;
			gchar** _tmp63_;
			gint _tmp63__length1;
			gchar* _tmp64_;
			gchar** _tmp65_;
			gint _tmp65__length1;
			const gchar* _tmp66_;
			gchar** _tmp67_;
			gint _tmp67__length1;
			const gchar* _tmp68_;
			_tmp58_ = _tmp57_ = g_strsplit_set (standard_error, ";", 2);
			updates_num = (_vala_array_free (updates_num, updates_num_length1, (GDestroyNotify) g_free), NULL);
			updates_num = _tmp58_;
			updates_num_length1 = _vala_array_length (_tmp57_);
			_updates_num_size_ = updates_num_length1;
			if (updates_num_length1 < 2) {
				updates_num = g_realloc (updates_num, 3 * sizeof(gchar *));
				updates_num[updates_num_length1++] = g_strdup ("0");
				updates_num[updates_num_length1] = NULL;
			}
			_tmp60_ = updates_num[0];
			g_message ("app.vala:1506: Number of upgrades: %s", _tmp60_);
			_tmp62_ = updates_num[1];
			g_message ("app.vala:1507: Number of security upgrades: %s", _tmp62_);
			if (!lxsession_parse_count (updates_num[0], &updates_normal))
				updates_normal = 0;
			if (!lxsession_parse_count (updates_num[1], &updates_urgent))
				updates_urgent = 0;
			number_updates = updates_normal + updates_urgent;
			if (number_updates == 1) {
				gchar* _tmp69_;
				gchar* _tmp70_;
				gchar* _tmp71_;
				_tmp69_ = g_strdup_printf ("%i", number_updates);
				_tmp70_ = _tmp69_;
				_tmp71_ = g_strconcat (_tmp70_, _ (" Update available"), NULL);
				_g_free0 (notification_text);
				notification_text = _tmp71_;
				_g_free0 (_tmp70_);
			} else {
				if (number_updates > 1) {
					gchar* _tmp72_;
					gchar* _tmp73_;
					gchar* _tmp74_;
					gchar* _tmp75_;
					gchar* _tmp76_;
					_tmp72_ = g_strdup_printf ("%i", number_updates);
					_tmp73_ = _tmp72_;
					_tmp74_ = g_strconcat (_tmp73_, " ", NULL);
					_tmp75_ = _tmp74_;
					_tmp76_ = g_strconcat (_tmp75_, _ ("Updates available"), NULL);
					_g_free0 (notification_text);
					notification_text = _tmp76_;
					_g_free0 (_tmp75_);
					_g_free0 (_tmp73_);
				}
			}
		}
	} else {
		updates_state = 1;
	}
	if (number_updates > 0) {
		LxsessionIconObject* _tmp77_;
		const gchar* _tmp78_;
		LxsessionIconObject* _tmp79_;
		g_message ("app.vala:1533: Activate icon because of updates available");
		_tmp77_ = self->priv->updates_icon;
		_tmp78_ = notification_text;
		lxsession_icon_object_set_notification_body (_tmp77_, _tmp78_);
		_tmp79_ = self->priv->updates_icon;
		lxsession_icon_object_activate (_tmp79_);
	}
	if (updates_urgent > 0) {
		LxsessionIconObject* _tmp80_;
		LxsessionIconObject* _tmp81_;
		const gchar* _tmp82_;
		g_message ("app.vala:1540: Set urgent icon");
		_tmp80_ = self->priv->updates_icon;
		lxsession_icon_object_set_icon (_tmp80_, "software-update-urgent");
		_tmp81_ = self->priv->updates_icon;
		_tmp82_ = notification_text;
		lxsession_icon_object_set_notification_body (_tmp81_, _tmp82_);
	}
	if (updates_state > 0) {
		LxsessionIconObject* _tmp83_;
		LxsessionIconObject* _tmp84_;
		const gchar* _tmp85_;
		LxsessionIconObject* _tmp86_;
		LxsessionIconObject* _tmp87_;
		const gchar* _tmp88_;
		LxsessionMenuObject* new_menu = NULL;
		LxsessionMenuObject* _tmp89_;
		LxsessionMenuItemObject* new_menu_item = NULL;
		LxsessionMenuItemObject* _tmp90_;
		LxsessionMenuItemObject* _tmp91_;
		const gchar* _tmp92_;
		LxsessionMenuItemObject* _tmp93_;
		LxsessionMenuItemObject* _tmp94_;
		LxsessionMenuObject* _tmp95_;
		LxsessionMenuItemObject* _tmp96_;
		LxsessionIconObject* _tmp97_;
		LxsessionMenuObject* _tmp98_;
		LxsessionIconObject* _tmp99_;
		g_message ("app.vala:1548: Problem in package state");
		_tmp83_ = self->priv->updates_icon;
		lxsession_icon_object_set_icon (_tmp83_, "software-update-urgent");
		_tmp84_ = self->priv->updates_icon;
		_tmp85_ = notification_text;
		lxsession_icon_object_set_notification_body (_tmp84_, _tmp85_);
		_tmp86_ = self->priv->updates_icon;
		lxsession_icon_object_clear_actions (_tmp86_);
		_tmp87_ = self->priv->updates_icon;
		_tmp88_ = launch_string;
		lxsession_icon_object_add_action (_tmp87_, "launch_upgrade_manager", _tmp88_, ____lambda7__lxsession_icon_object_action_callback, g_object_ref (self), g_object_unref);
		_tmp89_ = lxsession_menu_object_new ();
		new_menu = _tmp89_;
		_tmp90_ = lxsession_menu_item_object_new ();
		new_menu_item = _tmp90_;
		_tmp91_ = new_menu_item;
		_tmp92_ = launch_string;
		lxsession_menu_item_object_set_label (_tmp91_, _tmp92_);
		_tmp93_ = new_menu_item;
		g_signal_connect_object (_tmp93_, "activate", (GCallback) ____lambda8__lxsession_menu_item_object_activate, self, 0);
		_tmp94_ = new_menu_item;
		lxsession_menu_item_object_show (_tmp94_);
		_tmp95_ = new_menu;
		_tmp96_ = new_menu_item;
		lxsession_menu_object_add (_tmp95_, (GObject*) _tmp96_);
		_tmp97_ = self->priv->updates_icon;
		_tmp98_ = new_menu;
		lxsession_icon_object_set_menu (_tmp97_, _tmp98_);
		_tmp99_ = self->priv->updates_icon;
		lxsession_icon_object_activate (_tmp99_);
		_g_object_unref0 (new_menu_item);
		_g_object_unref0 (new_menu);
	}
	lxsession_updates_manager_app_check_language_support (self);
	lxsession_updates_manager_app_run_check_reboot (self);
	self->priv->lock_check = 0;
	result = FALSE;
	_g_free0 (error_string);
	_g_free0 (command);
	updates_num = (_vala_array_free (updates_num, updates_num_length1, (GDestroyNotify) g_free), NULL);
	_g_free0 (launch_string);
	_g_free0 (notification_text);
	_g_free0 (standard_error);
	_g_free0 (standard_output);
	block1_data_unref (_data1_);
	_data1_ = NULL;
	return result;
}

static void
_lxsession_updates_manager_app_run_check_g_file_monitor_changed (GFileMonitor* _sender,
                                                                 GFile* file,
                                                                 GFile* other_file,
                                                                 GFileMonitorEvent event_type,
                                                                 gpointer self)
{
	lxsession_updates_manager_app_run_check ((LxsessionUpdatesManagerApp*) self);
}

void
lxsession_updates_manager_app_setup_apt_config (LxsessionUpdatesManagerApp* self)
{
	GError* _inner_error0_ = NULL;
	g_return_if_fail (self != NULL);
	{
		const gchar* _tmp0_;
		GFile* _tmp1_;
		GFileMonitor* _tmp2_ = NULL;
		GFile* _tmp3_;
		GFileMonitor* _tmp4_;
		GFileMonitor* _tmp5_;
		GFileMonitor* _tmp6_;
		_tmp0_ = self->priv->apt_update_path;
		_tmp1_ = g_file_new_for_path (_tmp0_);
		_g_object_unref0 (self->priv->apt_update_file);
		self->priv->apt_update_file = _tmp1_;
		_tmp3_ = self->priv->apt_update_file;
		_tmp4_ = g_file_monitor_file (_tmp3_, G_FILE_MONITOR_NONE, NULL, &_inner_error0_);
		_tmp2_ = _tmp4_;
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			goto __catch0_g_error;
		}
		_tmp5_ = _tmp2_;
		_tmp2_ = NULL;
		_g_object_unref0 (self->priv->apt_update_monitor);
		self->priv->apt_update_monitor = _tmp5_;
		_tmp6_ = self->priv->apt_update_monitor;
		g_signal_connect_object (_tmp6_, "changed", (GCallback) _lxsession_updates_manager_app_run_check_g_file_monitor_changed, self, 0);
		g_message ("app.vala:1599: Monitoring apt changes");
		_g_object_unref0 (_tmp2_);
	}
	goto __finally0;
	__catch0_g_error:
	{
		GError* err = NULL;
		GError* _tmp7_;
		const gchar* _tmp8_;
		err = _inner_error0_;
		_inner_error0_ = NULL;
		_tmp7_ = err;
		_tmp8_ = _tmp7_->message;
		g_message ("app.vala:1603: %s", _tmp8_);
		_g_error_free0 (err);
	}
	__finally0:
	if (G_UNLIKELY (_inner_error0_ != NULL)) {
		g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
		g_clear_error (&_inner_error0_);
		return;
	}
	{
		const gchar* _tmp9_;
		GFile* _tmp10_;
		GFileMonitor* _tmp11_ = NULL;
		GFile* _tmp12_;
		GFileMonitor* _tmp13_;
		GFileMonitor* _tmp14_;
		GFileMonitor* _tmp15_;
		_tmp9_ = self->priv->dpkg_update_path;
		_tmp10_ = g_file_new_for_path (_tmp9_);
		_g_object_unref0 (self->priv->dpkg_update_file);
		self->priv->dpkg_update_file = _tmp10_;
		_tmp12_ = self->priv->dpkg_update_file;
		_tmp13_ = g_file_monitor_file (_tmp12_, G_FILE_MONITOR_NONE, NULL, &_inner_error0_);
		_tmp11_ = _tmp13_;
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			goto __catch1_g_error;
		}
		_tmp14_ = _tmp11_;
		_tmp11_ = NULL;
		_g_object_unref0 (self->priv->dpkg_update_monitor);
		self->priv->dpkg_update_monitor = _tmp14_;
		_tmp15_ = self->priv->dpkg_update_monitor;
		g_signal_connect_object (_tmp15_, "changed", (GCallback) _lxsession_updates_manager_app_run_check_g_file_monitor_changed, self, 0);
		g_message ("app.vala:1611: Monitoring dpkg changes");
		_g_object_unref0 (_tmp11_);
	}
	goto __finally1;
	__catch1_g_error:
	{
		GError* err = NULL;
		GError* _tmp16_;
		const gchar* _tmp17_;
		err = _inner_error0_;
		_inner_error0_ = NULL;
		_tmp16_ = err;
		_tmp17_ = _tmp16_->message;
		g_message ("app.vala:1615: %s", _tmp17_);
		_g_error_free0 (err);
	}
	__finally1:
	if (G_UNLIKELY (_inner_error0_ != NULL)) {
		g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
		g_clear_error (&_inner_error0_);
		return;
	}
	{
		const gchar* _tmp18_;
		GFile* _tmp19_;
		GFileMonitor* _tmp20_ = NULL;
		GFile* _tmp21_;
		GFileMonitor* _tmp22_;
		GFileMonitor* _tmp23_;
		GFileMonitor* _tmp24_;
		_tmp18_ = self->priv->dpkg_run_path;
		_tmp19_ = g_file_new_for_path (_tmp18_);
		_g_object_unref0 (self->priv->dpkg_run_file);
		self->priv->dpkg_run_file = _tmp19_;
		_tmp21_ = self->priv->dpkg_run_file;
		_tmp22_ = g_file_monitor_file (_tmp21_, G_FILE_MONITOR_NONE, NULL, &_inner_error0_);
		_tmp20_ = _tmp22_;
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			goto __catch2_g_error;
		}
		_tmp23_ = _tmp20_;
		_tmp20_ = NULL;
		_g_object_unref0 (self->priv->dpkg_run_monitor);
		self->priv->dpkg_run_monitor = _tmp23_;
		_tmp24_ = self->priv->dpkg_run_monitor;
		g_signal_connect_object (_tmp24_, "changed", (GCallback) _lxsession_updates_manager_app_run_check_g_file_monitor_changed, self, 0);
		g_message ("app.vala:1623: Monitoring dpkg run changes");
		_g_object_unref0 (_tmp20_);
	}
	goto __finally2;
	__catch2_g_error:
	{
		GError* err = NULL;
		GError* _tmp25_;
		const gchar* _tmp26_;
		err = _inner_error0_;
		_inner_error0_ = NULL;
		_tmp25_ = err;
		_tmp26_ = _tmp25_->message;
		g_message ("app.vala:1627: %s", _tmp26_);
		_g_error_free0 (err);
	}
	__finally2:
	if (G_UNLIKELY (_inner_error0_ != NULL)) {
		g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
		g_clear_error (&_inner_error0_);
		return;
	}
	{
		const gchar* _tmp27_;
		GFile* _tmp28_;
		GFileMonitor* _tmp29_ = NULL;
		GFile* _tmp30_;
		GFileMonitor* _tmp31_;
		GFileMonitor* _tmp32_;
		GFileMonitor* _tmp33_;
		_tmp27_ = self->priv->apt_lists_update_path;
		_tmp28_ = g_file_new_for_path (_tmp27_);
		_g_object_unref0 (self->priv->apt_lists_update_file);
		self->priv->apt_lists_update_file = _tmp28_;
		_tmp30_ = self->priv->apt_lists_update_file;
		_tmp31_ = g_file_monitor_directory (_tmp30_, G_FILE_MONITOR_NONE, NULL, &_inner_error0_);
		_tmp29_ = _tmp31_;
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			goto __catch3_g_error;
		}
		_tmp32_ = _tmp29_;
		_tmp29_ = NULL;
		_g_object_unref0 (self->priv->apt_lists_update_monitor);
		self->priv->apt_lists_update_monitor = _tmp32_;
		_tmp33_ = self->priv->apt_lists_update_monitor;
		g_signal_connect_object (_tmp33_, "changed", (GCallback) _lxsession_updates_manager_app_run_check_g_file_monitor_changed, self, 0);
		g_message ("app.vala:1635: Monitoring apt_lists changes");
		_g_object_unref0 (_tmp29_);
	}
	goto __finally3;
	__catch3_g_error:
	{
		GError* err = NULL;
		GError* _tmp34_;
		const gchar* _tmp35_;
		err = _inner_error0_;
		_inner_error0_ = NULL;
		_tmp34_ = err;
		_tmp35_ = _tmp34_->message;
		g_message ("app.vala:1639: %s", _tmp35_);
		_g_error_free0 (err);
	}
	__finally3:
	if (G_UNLIKELY (_inner_error0_ != NULL)) {
		g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
		g_clear_error (&_inner_error0_);
		return;
	}
}

static void
_lxsession_updates_manager_app_run_check_reboot_g_file_monitor_changed (GFileMonitor* _sender,
                                                                        GFile* file,
                                                                        GFile* other_file,
                                                                        GFileMonitorEvent event_type,
                                                                        gpointer self)
{
	lxsession_updates_manager_app_run_check_reboot ((LxsessionUpdatesManagerApp*) self);
}

void
lxsession_updates_manager_app_setup_reboot_config (LxsessionUpdatesManagerApp* self)
{
	GError* _inner_error0_ = NULL;
	g_return_if_fail (self != NULL);
	{
		const gchar* _tmp0_;
		GFile* _tmp1_;
		GFileMonitor* _tmp2_ = NULL;
		GFile* _tmp3_;
		GFileMonitor* _tmp4_;
		GFileMonitor* _tmp5_;
		GFileMonitor* _tmp6_;
		_tmp0_ = self->priv->reboot_path;
		_tmp1_ = g_file_new_for_path (_tmp0_);
		_g_object_unref0 (self->priv->reboot_file);
		self->priv->reboot_file = _tmp1_;
		_tmp3_ = self->priv->reboot_file;
		_tmp4_ = g_file_monitor_file (_tmp3_, G_FILE_MONITOR_NONE, NULL, &_inner_error0_);
		_tmp2_ = _tmp4_;
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			goto __catch0_g_error;
		}
		_tmp5_ = _tmp2_;
		_tmp2_ = NULL;
		_g_object_unref0 (self->priv->reboot_monitor);
		self->priv->reboot_monitor = _tmp5_;
		_tmp6_ = self->priv->reboot_monitor;
		g_signal_connect_object (_tmp6_, "changed", (GCallback) _lxsession_updates_manager_app_run_check_reboot_g_file_monitor_changed, self, 0);
		g_message ("app.vala:1650: Monitoring reboot changes");
		_g_object_unref0 (_tmp2_);
	}
	goto __finally0;
	__catch0_g_error:
	{
		GError* err = NULL;
		GError* _tmp7_;
		const gchar* _tmp8_;
		err = _inner_error0_;
		_inner_error0_ = NULL;
		_tmp7_ = err;
		_tmp8_ = _tmp7_->message;
		g_message ("app.vala:1654: %s", _tmp8_);
		_g_error_free0 (err);
	}
	__finally0:
	if (G_UNLIKELY (_inner_error0_ != NULL)) {
		g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
		g_clear_error (&_inner_error0_);
		return;
	}
}

gboolean
lxsession_updates_manager_app_check_lock_file (LxsessionUpdatesManagerApp* self,
                                               const gchar* check_file)
{
	gchar* lock_file_name = NULL;
	gchar* _tmp0_;
	gint fd = 0;
	const gchar* _tmp1_;
	struct flock fl = {0};
	gint fcntl_return = 0;
	struct flock _tmp13_;
	struct flock _tmp14_;
	gboolean result;
	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (check_file != NULL, FALSE);
	_tmp0_ = g_strdup (check_file);
	lock_file_name = _tmp0_;
	_tmp1_ = lock_file_name;
	fd = open (_tmp1_, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, (mode_t) 0);
	if (fd == -1) {
		const gchar* _tmp2_;
		gchar* _tmp3_;
		gchar* _tmp4_;
		gchar* _tmp5_;
		gchar* _tmp6_;
		gchar* _tmp7_;
		gchar* _tmp8_;
		gchar* _tmp9_;
		gchar* _tmp10_;
		gchar* _tmp11_;
		gchar* _tmp12_;
		_tmp2_ = lock_file_name;
		_tmp3_ = g_strconcat ("There was an error opening the file '", _tmp2_, NULL);
		_tmp4_ = _tmp3_;
		_tmp5_ = g_strconcat (_tmp4_, " (Error number ", NULL);
		_tmp6_ = _tmp5_;
		_tmp7_ = g_strdup_printf ("%i", errno);
		_tmp8_ = _tmp7_;
		_tmp9_ = g_strconcat (_tmp6_, _tmp8_, NULL);
		_tmp10_ = _tmp9_;
		_tmp11_ = g_strconcat (_tmp10_, ")\n", NULL);
		_tmp12_ = _tmp11_;
		g_print ("%s", _tmp12_);
		_g_free0 (_tmp12_);
		_g_free0 (_tmp10_);
		_g_free0 (_tmp8_);
		_g_free0 (_tmp6_);
		_g_free0 (_tmp4_);
		result = TRUE;
		_g_free0 (lock_file_name);
		return result;
	}
	{
		struct stat lock_stat;
		if (fstat (fd, &lock_stat) != 0 || !S_ISREG (lock_stat.st_mode)) {
			close (fd);
			_g_free0 (lock_file_name);
			return TRUE;
		}
	}
	memset (&fl, 0, sizeof (struct flock));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = (off_t) 100;
	fl.l_len = (off_t) 0;
	_tmp13_ = fl;
	fcntl_return = fcntl (fd, F_SETLK, &_tmp13_, NULL);
	if (fcntl_return == -1) {
		close (fd);
		result = TRUE;
		_g_free0 (lock_file_name);
		return result;
	}
	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = (off_t) 100;
	fl.l_len = (off_t) 0;
	_tmp14_ = fl;
	fcntl_return = fcntl (fd, F_SETLK, &_tmp14_, NULL);
	close (fd);
	result = FALSE;
	_g_free0 (lock_file_name);
	return result;
}

static void
lxsession_updates_manager_app_class_init (LxsessionUpdatesManagerAppClass * klass,
                                          gpointer klass_data)
{
	lxsession_updates_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionUpdatesManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_updates_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_updates_manager_app_finalize;
}

static void
lxsession_updates_manager_app_instance_init (LxsessionUpdatesManagerApp * self,
                                             gpointer klass)
{
	gchar* _tmp0_;
	gchar* _tmp1_;
	gchar* _tmp2_;
	gchar* _tmp3_;
	gchar* _tmp4_;
	gchar* _tmp5_;
	self->priv = lxsession_updates_manager_app_get_instance_private (self);
	_tmp0_ = g_strdup ("/var/lib/apt/periodic/update-success-stamp");
	self->priv->apt_update_path = _tmp0_;
	_tmp1_ = g_strdup ("/var/lib/dpkg/status");
	self->priv->dpkg_update_path = _tmp1_;
	_tmp2_ = g_strdup ("/var/lib/update-notifier/dpkg-run-stamp");
	self->priv->dpkg_run_path = _tmp2_;
	_tmp3_ = g_strdup ("/var/lib/apt/lists");
	self->priv->apt_lists_update_path = _tmp3_;
	_tmp4_ = g_strdup ("/var/run/reboot-required");
	self->priv->reboot_path = _tmp4_;
	_tmp5_ = g_strdup ("/var/lib/dpkg/lock");
	self->priv->dpkg_lock_file = _tmp5_;
	self->priv->lock_check = 0;
}

static void
lxsession_updates_manager_app_finalize (GObject * obj)
{
	LxsessionUpdatesManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_UPDATES_MANAGER_APP, LxsessionUpdatesManagerApp);
	_g_free0 (self->priv->updatesmanager_command);
	_g_object_unref0 (self->priv->updates_icon);
	_g_object_unref0 (self->priv->icon_menu);
	_g_object_unref0 (self->priv->language_icon);
	_g_object_unref0 (self->priv->reboot_icon);
	_g_free0 (self->priv->apt_update_path);
	_g_object_unref0 (self->priv->apt_update_file);
	_g_object_unref0 (self->priv->apt_update_monitor);
	_g_free0 (self->priv->dpkg_update_path);
	_g_object_unref0 (self->priv->dpkg_update_file);
	_g_object_unref0 (self->priv->dpkg_update_monitor);
	_g_free0 (self->priv->dpkg_run_path);
	_g_object_unref0 (self->priv->dpkg_run_file);
	_g_object_unref0 (self->priv->dpkg_run_monitor);
	_g_free0 (self->priv->apt_lists_update_path);
	_g_object_unref0 (self->priv->apt_lists_update_file);
	_g_object_unref0 (self->priv->apt_lists_update_monitor);
	_g_free0 (self->priv->reboot_path);
	_g_object_unref0 (self->priv->reboot_file);
	_g_object_unref0 (self->priv->reboot_monitor);
	_g_free0 (self->priv->dpkg_lock_file);
	_g_free0 (self->priv->apt_check_stdout);
	_g_free0 (self->priv->apt_check_stderr);
	G_OBJECT_CLASS (lxsession_updates_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_updates_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionUpdatesManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_updates_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionUpdatesManagerApp), 0, (GInstanceInitFunc) lxsession_updates_manager_app_instance_init, NULL };
	GType lxsession_updates_manager_app_type_id;
	lxsession_updates_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionUpdatesManagerApp", &g_define_type_info, 0);
	LxsessionUpdatesManagerApp_private_offset = g_type_add_instance_private (lxsession_updates_manager_app_type_id, sizeof (LxsessionUpdatesManagerAppPrivate));
	return lxsession_updates_manager_app_type_id;
}

GType
lxsession_updates_manager_app_get_type (void)
{
	static volatile gsize lxsession_updates_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_updates_manager_app_type_id__once)) {
		GType lxsession_updates_manager_app_type_id;
		lxsession_updates_manager_app_type_id = lxsession_updates_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_updates_manager_app_type_id__once, lxsession_updates_manager_app_type_id);
	}
	return lxsession_updates_manager_app_type_id__once;
}

static inline gpointer
lxsession_crash_manager_app_get_instance_private (LxsessionCrashManagerApp* self)
{
	return G_STRUCT_MEMBER_P (self, LxsessionCrashManagerApp_private_offset);
}

LxsessionCrashManagerApp*
lxsession_crash_manager_app_construct (GType object_type)
{
	LxsessionCrashManagerApp * self = NULL;
	self = (LxsessionCrashManagerApp*) lxsession_simple_app_object_construct (object_type);
	lxsession_app_object_init ((LxsessionAppObject*) self);
	return self;
}

LxsessionCrashManagerApp*
lxsession_crash_manager_app_new (void)
{
	return lxsession_crash_manager_app_construct (LXSESSION_TYPE_CRASH_MANAGER_APP);
}

static void
lxsession_crash_manager_app_real_read_settings (LxsessionAppObject* base)
{
	LxsessionCrashManagerApp * self;
	LxsessionLxsessionConfigKeyFile* _tmp0_;
	gchar* _tmp1_;
	const gchar* _tmp2_;
	const gchar* _tmp3_;
	GQuark _tmp5_ = 0U;
	static GQuark _tmp4_label0 = 0;
	static GQuark _tmp4_label1 = 0;
	static GQuark _tmp4_label2 = 0;
	static GQuark _tmp4_label3 = 0;
	self = (LxsessionCrashManagerApp*) base;
	_tmp0_ = lxsession_global_settings;
	_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "crash_manager", "command");
	_g_free0 (self->priv->crash_manager_command);
	self->priv->crash_manager_command = _tmp1_;
	_tmp2_ = self->priv->crash_manager_command;
	_tmp3_ = _tmp2_;
	_tmp5_ = (NULL == _tmp3_) ? 0 : g_quark_from_string (_tmp3_);
	if (_tmp5_ == ((0 != _tmp4_label0) ? _tmp4_label0 : (_tmp4_label0 = g_quark_from_static_string (NULL)))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label1) ? _tmp4_label1 : (_tmp4_label1 = g_quark_from_static_string ("")))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label2) ? _tmp4_label2 : (_tmp4_label2 = g_quark_from_static_string (" ")))) {
		switch (0) {
			default:
			{
				break;
			}
		}
	} else if (_tmp5_ == ((0 != _tmp4_label3) ? _tmp4_label3 : (_tmp4_label3 = g_quark_from_static_string ("apport-gtk")))) {
		switch (0) {
			default:
			{
				lxsession_crash_manager_app_setup_crash_log_config (self);
				lxsession_crash_manager_app_run_crash_check (self);
				break;
			}
		}
	} else {
		switch (0) {
			default:
			{
				gchar** create_command = NULL;
				const gchar* _tmp6_;
				gchar** _tmp7_;
				gchar** _tmp8_;
				gint create_command_length1;
				gint _create_command_size_;
				gchar** _tmp9_;
				gint _tmp9__length1;
				const gchar* _tmp10_;
				gchar** _tmp11_;
				gint _tmp11__length1;
				_tmp6_ = self->priv->crash_manager_command;
				_tmp8_ = _tmp7_ = lxsession_parse_command (_tmp6_, " ", 0);
				create_command = _tmp8_;
				create_command_length1 = _vala_array_length (_tmp7_);
				_create_command_size_ = create_command_length1;
				_tmp9_ = create_command;
				_tmp9__length1 = create_command_length1;
				_tmp10_ = _tmp9_[0];
				lxsession_app_object_set_name ((LxsessionAppObject*) self, _tmp10_);
				_tmp11_ = create_command;
				_tmp11__length1 = create_command_length1;
				lxsession_app_object_set_command ((LxsessionAppObject*) self, _tmp11_, _tmp11__length1);
				create_command = (_vala_array_free (create_command, create_command_length1, (GDestroyNotify) g_free), NULL);
				break;
			}
		}
	}
}

void
lxsession_crash_manager_app_test_activate (LxsessionCrashManagerApp* self)
{
	LxsessionIconObject* _tmp0_;
	g_return_if_fail (self != NULL);
	_tmp0_ = self->priv->crash_icon;
	if (_tmp0_ != NULL) {
		LxsessionIconObject* _tmp1_;
		_tmp1_ = self->priv->crash_icon;
		lxsession_icon_object_activate (_tmp1_);
	}
}

void
lxsession_crash_manager_app_test_inactivate (LxsessionCrashManagerApp* self)
{
	LxsessionIconObject* _tmp0_;
	g_return_if_fail (self != NULL);
	_tmp0_ = self->priv->crash_icon;
	if (_tmp0_ != NULL) {
		LxsessionIconObject* _tmp1_;
		_tmp1_ = self->priv->crash_icon;
		lxsession_icon_object_inactivate (_tmp1_);
	}
}

static void
_lxsession_crash_manager_app_run_crash_check_g_file_monitor_changed (GFileMonitor* _sender,
                                                                     GFile* file,
                                                                     GFile* other_file,
                                                                     GFileMonitorEvent event_type,
                                                                     gpointer self)
{
	lxsession_crash_manager_app_run_crash_check ((LxsessionCrashManagerApp*) self);
}

void
lxsession_crash_manager_app_setup_crash_log_config (LxsessionCrashManagerApp* self)
{
	GError* _inner_error0_ = NULL;
	g_return_if_fail (self != NULL);
	{
		const gchar* _tmp0_;
		GFile* _tmp1_;
		GFileMonitor* _tmp2_ = NULL;
		GFile* _tmp3_;
		GFileMonitor* _tmp4_;
		GFileMonitor* _tmp5_;
		GFileMonitor* _tmp6_;
		_tmp0_ = self->priv->crash_dir_path;
		_tmp1_ = g_file_new_for_path (_tmp0_);
		_g_object_unref0 (self->priv->crash_dir_file);
		self->priv->crash_dir_file = _tmp1_;
		_tmp3_ = self->priv->crash_dir_file;
		_tmp4_ = g_file_monitor_directory (_tmp3_, G_FILE_MONITOR_NONE, NULL, &_inner_error0_);
		_tmp2_ = _tmp4_;
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			goto __catch0_g_error;
		}
		_tmp5_ = _tmp2_;
		_tmp2_ = NULL;
		_g_object_unref0 (self->priv->crash_dir_monitor);
		self->priv->crash_dir_monitor = _tmp5_;
		_tmp6_ = self->priv->crash_dir_monitor;
		g_signal_connect_object (_tmp6_, "changed", (GCallback) _lxsession_crash_manager_app_run_crash_check_g_file_monitor_changed, self, 0);
		g_message ("app.vala:1757: Monitoring crash dir changes");
		_g_object_unref0 (_tmp2_);
	}
	goto __finally0;
	__catch0_g_error:
	{
		GError* err = NULL;
		GError* _tmp7_;
		const gchar* _tmp8_;
		err = _inner_error0_;
		_inner_error0_ = NULL;
		_tmp7_ = err;
		_tmp8_ = _tmp7_->message;
		g_message ("app.vala:1761: %s", _tmp8_);
		_g_error_free0 (err);
	}
	__finally0:
	if (G_UNLIKELY (_inner_error0_ != NULL)) {
		g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
		g_clear_error (&_inner_error0_);
		return;
	}
}

static gboolean
_lxsession_crash_manager_app_on_crash_file_change_gsource_func (gpointer self)
{
	gboolean result;
	result = lxsession_crash_manager_app_on_crash_file_change ((LxsessionCrashManagerApp*) self);
	return result;
}

void
lxsession_crash_manager_app_run_crash_check (LxsessionCrashManagerApp* self)
{
	g_return_if_fail (self != NULL);
	if (self->priv->lock_crash_check == 0) {
		gint timeout = 0;
		LxsessionLxsessionConfigKeyFile* _tmp0_;
		gchar* _tmp1_;
		gchar* _tmp2_;
		gboolean _tmp3_;
		self->priv->lock_crash_check = 1;
		timeout = 60;
		_tmp0_ = lxsession_global_settings;
		_tmp1_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp0_, "Session", "crash_manager", "timeout");
		_tmp2_ = _tmp1_;
		_tmp3_ = _tmp2_ != NULL;
		_g_free0 (_tmp2_);
		if (_tmp3_) {
			LxsessionLxsessionConfigKeyFile* _tmp4_;
			gchar* _tmp5_;
			gchar* _tmp6_;
			_tmp4_ = lxsession_global_settings;
			_tmp5_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp4_, "Session", "crash_manager", "timeout");
			_tmp6_ = _tmp5_;
			if (!lxsession_parse_timeout (_tmp6_, &timeout))
				timeout = 60;
			_g_free0 (_tmp6_);
		}
		g_timeout_add_seconds_full (G_PRIORITY_DEFAULT, (guint) timeout, _lxsession_crash_manager_app_on_crash_file_change_gsource_func, g_object_ref (self), g_object_unref);
	}
}

static gchar*
string_replace (const gchar* self,
                const gchar* old,
                const gchar* replacement)
{
	gboolean _tmp0_ = FALSE;
	gboolean _tmp1_ = FALSE;
	GError* _inner_error0_ = NULL;
	gchar* result;
	g_return_val_if_fail (self != NULL, NULL);
	g_return_val_if_fail (old != NULL, NULL);
	g_return_val_if_fail (replacement != NULL, NULL);
	if ((*((gchar*) self)) == '\0') {
		_tmp1_ = TRUE;
	} else {
		_tmp1_ = (*((gchar*) old)) == '\0';
	}
	if (_tmp1_) {
		_tmp0_ = TRUE;
	} else {
		_tmp0_ = g_strcmp0 (old, replacement) == 0;
	}
	if (_tmp0_) {
		gchar* _tmp2_;
		_tmp2_ = g_strdup (self);
		result = _tmp2_;
		return result;
	}
	{
		GRegex* regex = NULL;
		gchar* _tmp3_;
		gchar* _tmp4_;
		GRegex* _tmp5_;
		GRegex* _tmp6_;
		gchar* _tmp7_ = NULL;
		GRegex* _tmp8_;
		gchar* _tmp9_;
		gchar* _tmp10_;
		_tmp3_ = g_regex_escape_string (old, -1);
		_tmp4_ = _tmp3_;
		_tmp5_ = g_regex_new (_tmp4_, 0, 0, &_inner_error0_);
		_tmp6_ = _tmp5_;
		_g_free0 (_tmp4_);
		regex = _tmp6_;
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			if (_inner_error0_->domain == G_REGEX_ERROR) {
				goto __catch0_g_regex_error;
			}
			g_critical ("file %s: line %d: unexpected error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
			g_clear_error (&_inner_error0_);
			return NULL;
		}
		_tmp8_ = regex;
		_tmp9_ = g_regex_replace_literal (_tmp8_, self, (gssize) -1, 0, replacement, 0, &_inner_error0_);
		_tmp7_ = _tmp9_;
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			_g_regex_unref0 (regex);
			if (_inner_error0_->domain == G_REGEX_ERROR) {
				goto __catch0_g_regex_error;
			}
			g_critical ("file %s: line %d: unexpected error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
			g_clear_error (&_inner_error0_);
			return NULL;
		}
		_tmp10_ = _tmp7_;
		_tmp7_ = NULL;
		result = _tmp10_;
		_g_free0 (_tmp7_);
		_g_regex_unref0 (regex);
		return result;
	}
	goto __finally0;
	__catch0_g_regex_error:
	{
		g_clear_error (&_inner_error0_);
		g_assert_not_reached ();
	}
	__finally0:
	g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
	g_clear_error (&_inner_error0_);
	return NULL;
}

static void
_vala_array_add1 (gchar** * array,
                  gint* length,
                  gint* size,
                  gchar* value)
{
	if ((*length) == (*size)) {
		*size = (*size) ? (2 * (*size)) : 4;
		*array = g_renew (gchar*, *array, (*size) + 1);
	}
	(*array)[(*length)++] = value;
	(*array)[*length] = NULL;
}

static inline void
_g_list_free__g_free0_ (GList* self)
{
	g_list_free_full (self, (GDestroyNotify) _g_free0_);
}

static gboolean
_vala_string_array_contains (gchar* * stack,
                             gssize stack_length,
                             const gchar* needle)
{
	gssize i;
	for (i = 0; i < stack_length; i++) {
		if (g_strcmp0 (stack[i], needle) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

GList*
lxsession_crash_manager_app_check_crash_file (LxsessionCrashManagerApp* self)
{
	GList* crash_list = NULL;
	GList* final_crash_list = NULL;
	gchar** uploaded_list = NULL;
	gchar** _tmp0_;
	gint uploaded_list_length1;
	gint _uploaded_list_size_;
	gchar* file_name = NULL;
	GFile* _tmp1_;
	GList* _tmp27_;
	GError* _inner_error0_ = NULL;
	GList* result;
	g_return_val_if_fail (self != NULL, NULL);
	crash_list = NULL;
	final_crash_list = NULL;
	_tmp0_ = g_new0 (gchar*, 0 + 1);
	uploaded_list = _tmp0_;
	uploaded_list_length1 = 0;
	_uploaded_list_size_ = uploaded_list_length1;
	_tmp1_ = self->priv->crash_dir_file;
	if (_tmp1_ != NULL) {
		{
			GFile* directory = NULL;
			const gchar* _tmp2_;
			GFile* _tmp3_;
			GFileEnumerator* enumerator = NULL;
			GFile* _tmp4_;
			GFileEnumerator* _tmp5_;
			GFileInfo* file_info = NULL;
			_tmp2_ = self->priv->crash_dir_path;
			_tmp3_ = g_file_new_for_path (_tmp2_);
			directory = _tmp3_;
			_tmp4_ = directory;
			_tmp5_ = g_file_enumerate_children (_tmp4_, G_FILE_ATTRIBUTE_STANDARD_NAME, 0, NULL, &_inner_error0_);
			enumerator = _tmp5_;
			if (G_UNLIKELY (_inner_error0_ != NULL)) {
				_g_object_unref0 (directory);
				goto __catch0_g_error;
			}
			while (TRUE) {
				GFileInfo* _tmp6_ = NULL;
				GFileEnumerator* _tmp7_;
				GFileInfo* _tmp8_;
				GFileInfo* _tmp9_;
				GFileInfo* _tmp10_;
				GFileInfo* _tmp11_;
				const gchar* _tmp12_;
				gchar* _tmp13_;
				const gchar* _tmp14_;
				_tmp7_ = enumerator;
				_tmp8_ = g_file_enumerator_next_file (_tmp7_, NULL, &_inner_error0_);
				_tmp6_ = _tmp8_;
				if (G_UNLIKELY (_inner_error0_ != NULL)) {
					_g_object_unref0 (file_info);
					_g_object_unref0 (enumerator);
					_g_object_unref0 (directory);
					goto __catch0_g_error;
				}
				_tmp9_ = _tmp6_;
				_tmp6_ = NULL;
				_g_object_unref0 (file_info);
				file_info = _tmp9_;
				_tmp10_ = file_info;
				if (!(_tmp10_ != NULL)) {
					_g_object_unref0 (_tmp6_);
					break;
				}
				_tmp11_ = file_info;
				_tmp12_ = g_file_info_get_name (_tmp11_);
				_tmp13_ = g_strdup (_tmp12_);
				_g_free0 (file_name);
				file_name = _tmp13_;
				_tmp14_ = file_name;
				if (g_strcmp0 (_tmp14_, ".lock") != 0) {
					const gchar* _tmp15_;
					_tmp15_ = file_name;
					if (g_str_has_suffix (_tmp15_, ".crash")) {
						const gchar* _tmp16_;
						const gchar* _tmp17_;
						gchar* _tmp18_;
						_tmp16_ = file_name;
						g_message ("app.vala:1801: Add to crash_list: %s", _tmp16_);
						_tmp17_ = file_name;
						_tmp18_ = g_strdup (_tmp17_);
						crash_list = g_list_append (crash_list, _tmp18_);
					} else {
						const gchar* _tmp19_;
						_tmp19_ = file_name;
						if (g_str_has_suffix (_tmp19_, ".uploaded")) {
							const gchar* _tmp20_;
							gchar* _tmp21_;
							const gchar* _tmp22_;
							gchar* _tmp23_;
							gchar* _tmp24_;
							_tmp20_ = file_name;
							_tmp21_ = string_replace (_tmp20_, ".uploaded", ".crash");
							_vala_array_add1 (&uploaded_list, &uploaded_list_length1, &_uploaded_list_size_, _tmp21_);
							_tmp22_ = file_name;
							_tmp23_ = string_replace (_tmp22_, ".uploaded", ".crash");
							_tmp24_ = _tmp23_;
							g_message ("app.vala:1807: Added to upload_list: %s", _tmp24_);
							_g_free0 (_tmp24_);
						}
					}
				}
				_g_object_unref0 (_tmp6_);
			}
			_g_object_unref0 (file_info);
			_g_object_unref0 (enumerator);
			_g_object_unref0 (directory);
		}
		goto __finally0;
		__catch0_g_error:
		{
			GError* e = NULL;
			GError* _tmp25_;
			const gchar* _tmp26_;
			e = _inner_error0_;
			_inner_error0_ = NULL;
			_tmp25_ = e;
			_tmp26_ = _tmp25_->message;
			g_message ("app.vala:1814: Error: %s\n", _tmp26_);
			_g_error_free0 (e);
		}
		__finally0:
		if (G_UNLIKELY (_inner_error0_ != NULL)) {
			_g_free0 (file_name);
			uploaded_list = (_vala_array_free (uploaded_list, uploaded_list_length1, (GDestroyNotify) g_free), NULL);
			(final_crash_list == NULL) ? NULL : (final_crash_list = (_g_list_free__g_free0_ (final_crash_list), NULL));
			(crash_list == NULL) ? NULL : (crash_list = (_g_list_free__g_free0_ (crash_list), NULL));
			g_critical ("file %s: line %d: uncaught error: %s (%s, %d)", __FILE__, __LINE__, _inner_error0_->message, g_quark_to_string (_inner_error0_->domain), _inner_error0_->code);
			g_clear_error (&_inner_error0_);
			return NULL;
		}
	}
	_tmp27_ = crash_list;
	{
		GList* element_collection = NULL;
		GList* element_it = NULL;
		element_collection = _tmp27_;
		for (element_it = element_collection; element_it != NULL; element_it = element_it->next) {
			gchar* _tmp28_;
			gchar* element = NULL;
			_tmp28_ = g_strdup ((const gchar*) element_it->data);
			element = _tmp28_;
			{
				const gchar* _tmp29_;
				gchar** _tmp30_;
				gint _tmp30__length1;
				_tmp29_ = element;
				g_message ("app.vala:1820: Check element in crash_list: %s", _tmp29_);
				_tmp30_ = uploaded_list;
				_tmp30__length1 = uploaded_list_length1;
				if (_tmp30_ != NULL) {
					const gchar* _tmp31_;
					gchar** _tmp32_;
					gint _tmp32__length1;
					_tmp31_ = element;
					_tmp32_ = uploaded_list;
					_tmp32__length1 = uploaded_list_length1;
					if (_vala_string_array_contains (_tmp32_, _tmp32__length1, _tmp31_)) {
						const gchar* _tmp33_;
						_tmp33_ = element;
						g_message ("app.vala:1825: Remove element in crash_list: %s", _tmp33_);
					} else {
						const gchar* _tmp34_;
						gchar* _tmp35_;
						_tmp34_ = element;
						_tmp35_ = g_strdup (_tmp34_);
						final_crash_list = g_list_append (final_crash_list, _tmp35_);
					}
				} else {
					const gchar* _tmp36_;
					gchar* _tmp37_;
					_tmp36_ = element;
					_tmp37_ = g_strdup (_tmp36_);
					final_crash_list = g_list_append (final_crash_list, _tmp37_);
				}
				_g_free0 (element);
			}
		}
	}
	result = final_crash_list;
	_g_free0 (file_name);
	uploaded_list = (_vala_array_free (uploaded_list, uploaded_list_length1, (GDestroyNotify) g_free), NULL);
	(crash_list == NULL) ? NULL : (crash_list = (_g_list_free__g_free0_ (crash_list), NULL));
	return result;
}

static gchar*
lxsession_crash_path_arg (const gchar *directory, const gchar *name)
{
    gchar *path;
    gchar *quoted;

    if (directory == NULL || name == NULL || !g_path_is_absolute(directory) ||
        g_file_test(directory, G_FILE_TEST_IS_DIR) == FALSE ||
        name[0] == '\0' || strchr(name, '/') != NULL || strchr(name, '\\') != NULL ||
        !g_str_has_suffix(name, ".crash"))
        return NULL;
    for (const gchar *p = name; *p != '\0'; p++)
    {
        if (g_ascii_iscntrl(*p))
            return NULL;
    }
    path = g_build_filename(directory, name, NULL);
    quoted = g_shell_quote(path);
    g_free(path);
    return quoted;
}

static void
lxsession_crash_close_child (GPid pid, gint status, gpointer data)
{
    (void)status;
    (void)data;
    g_spawn_close_pid(pid);
}

void
lxsession_crash_manager_app_crash_report_launch (LxsessionCrashManagerApp* self,
                                                 const gchar* command)
{
	gchar **argv = NULL;
	GError *error = NULL;
	GPid pid = 0;
	g_return_if_fail (self != NULL);
	g_return_if_fail (command != NULL);
	if (!g_shell_parse_argv(command, NULL, &argv, &error) ||
	    argv == NULL || argv[0] == NULL || argv[0][0] == '\0')
	{
		g_warning("app.vala: refusing invalid crash command: %s",
		          error != NULL ? error->message : "empty command");
		g_clear_error(&error);
		g_strfreev(argv);
		return;
	}
	if (!g_spawn_async(NULL, argv, NULL,
	                   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
	                   NULL, NULL, &pid, &error))
	{
		g_warning("app.vala: unable to launch crash command: %s", error->message);
		g_clear_error(&error);
	}
	else
	{
		g_child_watch_add_full(G_PRIORITY_DEFAULT_IDLE, pid,
		                       lxsession_crash_close_child, NULL, NULL);
		g_message("app.vala: launched crash command");
	}
	g_strfreev(argv);
}

static Block2Data*
block2_data_ref (Block2Data* _data2_)
{
	g_atomic_int_inc (&_data2_->_ref_count_);
	return _data2_;
}

static void
block2_data_unref (void * _userdata_)
{
	Block2Data* _data2_;
	_data2_ = (Block2Data*) _userdata_;
	if (g_atomic_int_dec_and_test (&_data2_->_ref_count_)) {
		LxsessionCrashManagerApp* self;
		self = _data2_->self;
		_g_free0 (_data2_->command);
		_g_object_unref0 (self);
		g_slice_free (Block2Data, _data2_);
	}
}

static void
_____lambda13_ (Block2Data* _data2_)
{
	LxsessionCrashManagerApp* self;
	const gchar* _tmp0_;
	self = _data2_->self;
	_tmp0_ = _data2_->command;
	lxsession_crash_manager_app_crash_report_launch (self, _tmp0_);
}

static void
______lambda13__lxsession_menu_item_object_activate (LxsessionMenuItemObject* _sender,
                                                     gpointer self)
{
	_____lambda13_ (self);
}

LxsessionMenuObject*
lxsession_crash_manager_app_build_menu (LxsessionCrashManagerApp* self,
                                        GList* crash_list)
{
	guint len = 0U;
	LxsessionMenuObject* menu = NULL;
	LxsessionMenuObject* _tmp0_;
	LxsessionMenuObject* result;
	g_return_val_if_fail (self != NULL, NULL);
	len = g_list_length (crash_list);
	_tmp0_ = lxsession_menu_object_new ();
	menu = _tmp0_;
	if (len >= ((guint) 1)) {
		{
			GList* element_collection = NULL;
			GList* element_it = NULL;
			element_collection = crash_list;
			for (element_it = element_collection; element_it != NULL; element_it = element_it->next) {
				gchar* _tmp1_;
				gchar* element = NULL;
				_tmp1_ = g_strdup ((const gchar*) element_it->data);
				element = _tmp1_;
				{
					Block2Data* _data2_;
					const gchar* _tmp2_;
					gchar* _tmp3_;
					gchar* _tmp4_;
					const gchar* _tmp5_;
					gchar* _tmp6_;
					gchar* _tmp7_;
					LxsessionMenuItemObject* menu_item = NULL;
					LxsessionMenuItemObject* _tmp8_;
					LxsessionMenuItemObject* _tmp9_;
					const gchar* _tmp10_;
					gchar* _tmp11_;
					gchar* _tmp12_;
					LxsessionMenuItemObject* _tmp13_;
					LxsessionMenuItemObject* _tmp14_;
					LxsessionMenuObject* _tmp15_;
					LxsessionMenuItemObject* _tmp16_;
					_data2_ = g_slice_new0 (Block2Data);
					_data2_->_ref_count_ = 1;
					_data2_->self = g_object_ref (self);
					_tmp2_ = self->priv->crash_dir_path;
					_tmp3_ = lxsession_crash_path_arg(_tmp2_, element);
					if (_tmp3_ == NULL)
						_tmp3_ = g_strdup("");
					_tmp4_ = g_strconcat (PACKAGE_BIN_DIR "/pkexec " PACKAGE_DATA_DIR "/apport/apport-gtk ", _tmp3_, NULL);
					_g_free0 (_tmp3_);
					_tmp7_ = _tmp4_;
					_data2_->command = _tmp7_;
					_tmp8_ = lxsession_menu_item_object_new ();
					menu_item = _tmp8_;
					_tmp9_ = menu_item;
					_tmp10_ = element;
					_tmp11_ = g_strconcat (_ ("Report "), _tmp10_, NULL);
					_tmp12_ = _tmp11_;
					lxsession_menu_item_object_set_label (_tmp9_, _tmp12_);
					_g_free0 (_tmp12_);
					_tmp13_ = menu_item;
					g_signal_connect_data (_tmp13_, "activate", (GCallback) ______lambda13__lxsession_menu_item_object_activate, block2_data_ref (_data2_), (GClosureNotify) block2_data_unref, 0);
					_tmp14_ = menu_item;
					lxsession_menu_item_object_show (_tmp14_);
					_tmp15_ = menu;
					_tmp16_ = menu_item;
					lxsession_menu_object_add (_tmp15_, (GObject*) _tmp16_);
					_g_object_unref0 (menu_item);
					_g_free0 (element);
					block2_data_unref (_data2_);
					_data2_ = NULL;
				}
			}
		}
	}
	result = menu;
	return result;
}

static Block3Data*
block3_data_ref (Block3Data* _data3_)
{
	g_atomic_int_inc (&_data3_->_ref_count_);
	return _data3_;
}

static void
block3_data_unref (void * _userdata_)
{
	Block3Data* _data3_;
	_data3_ = (Block3Data*) _userdata_;
	if (g_atomic_int_dec_and_test (&_data3_->_ref_count_)) {
		LxsessionCrashManagerApp* self;
		self = _data3_->self;
		_g_free0 (_data3_->remove_command);
		_g_object_unref0 (self);
		g_slice_free (Block3Data, _data3_);
	}
}

static void
____lambda14_ (Block3Data* _data3_)
{
	LxsessionCrashManagerApp* self;
	const gchar* _tmp0_;
	self = _data3_->self;
	_tmp0_ = _data3_->remove_command;
	lxsession_crash_manager_app_crash_report_launch (self, _tmp0_);
}

static void
_____lambda14__lxsession_icon_object_action_callback (gpointer self)
{
	____lambda14_ (self);
}

gboolean
lxsession_crash_manager_app_on_crash_file_change (LxsessionCrashManagerApp* self)
{
	GList* crash_list = NULL;
	guint len = 0U;
	LxsessionMenuObject* crash_menu = NULL;
	GList* _tmp0_;
	GList* _tmp1_;
	guint last_item = 0U;
	gboolean result;
	g_return_val_if_fail (self != NULL, FALSE);
	self->priv->lock_crash_check = 1;
	_tmp0_ = lxsession_crash_manager_app_check_crash_file (self);
	(crash_list == NULL) ? NULL : (crash_list = (_g_list_free__g_free0_ (crash_list), NULL));
	crash_list = _tmp0_;
	_tmp1_ = crash_list;
	len = g_list_length (_tmp1_);
	last_item = len > 0 ? len - 1 : 0;
	if (len >= ((guint) 1)) {
		Block3Data* _data3_;
		GList* _tmp2_;
		LxsessionMenuObject* _tmp3_;
		gchar* command = NULL;
		const gchar* _tmp4_;
		gchar* _tmp5_;
		gchar* _tmp6_;
		GList* _tmp7_;
		gconstpointer _tmp8_;
		gchar* _tmp9_;
		gchar* _tmp10_;
		const gchar* _tmp11_;
		gchar* _tmp12_;
		gchar* _tmp13_;
		GList* _tmp14_;
		gconstpointer _tmp15_;
		gchar* _tmp16_;
		gchar* _tmp17_;
		gchar* launch_string = NULL;
		gchar* _tmp18_;
		gchar* remove_string = NULL;
		gchar* _tmp19_;
		LxsessionIconObject* _tmp20_;
		LxsessionIconObject* _tmp24_;
		LxsessionMenuObject* _tmp25_;
		LxsessionLxsessionConfigKeyFile* _tmp26_;
		gchar* _tmp27_;
		gchar* _tmp28_;
		gboolean _tmp29_;
		LxsessionIconObject* _tmp32_;
		_data3_ = g_slice_new0 (Block3Data);
		_data3_->_ref_count_ = 1;
		_data3_->self = g_object_ref (self);
		_tmp2_ = crash_list;
		_tmp3_ = lxsession_crash_manager_app_build_menu (self, _tmp2_);
		_g_object_unref0 (crash_menu);
		crash_menu = _tmp3_;
		_tmp4_ = self->priv->crash_dir_path;
		_tmp5_ = lxsession_crash_path_arg(_tmp4_, (const gchar*) g_list_nth_data(crash_list, last_item));
		if (_tmp5_ == NULL)
			_tmp5_ = g_strdup("");
		_tmp6_ = g_strconcat (PACKAGE_BIN_DIR "/pkexec " PACKAGE_DATA_DIR "/apport/apport-gtk ", _tmp5_, NULL);
		_g_free0 (_tmp5_);
		command = _tmp6_;
		_tmp11_ = self->priv->crash_dir_path;
		_tmp12_ = lxsession_crash_path_arg(_tmp11_, (const gchar*) g_list_nth_data(crash_list, last_item));
		if (_tmp12_ == NULL)
			_tmp12_ = g_strdup("");
		_tmp13_ = g_strconcat (PACKAGE_BIN_DIR "/pkexec rm -f ", _tmp12_, NULL);
		_g_free0 (_tmp12_);
		_data3_->remove_command = _tmp13_;
		_tmp18_ = g_strdup ("Report last crash");
		launch_string = _tmp18_;
		_tmp19_ = g_strdup ("Remove last crash");
		remove_string = _tmp19_;
		_tmp20_ = self->priv->crash_icon;
		if (_tmp20_ == NULL) {
			LxsessionMenuObject* _tmp21_;
			LxsessionIconObject* _tmp22_;
			LxsessionIconObject* _tmp23_;
			_tmp21_ = crash_menu;
			_tmp22_ = lxsession_icon_object_new ("CrashIcon", "apport", _ ("Crash files available for report"), _tmp21_);
			_g_object_unref0 (self->priv->crash_icon);
			self->priv->crash_icon = _tmp22_;
			_tmp23_ = self->priv->crash_icon;
			lxsession_icon_object_init (_tmp23_);
		}
		_tmp24_ = self->priv->crash_icon;
		_tmp25_ = crash_menu;
		lxsession_icon_object_set_menu (_tmp24_, _tmp25_);
		_tmp26_ = lxsession_global_settings;
		_tmp27_ = lxsession_lxsession_config_get_item_string ((LxsessionLxsessionConfig*) _tmp26_, "Session", "crash_manager", "dev_mode");
		_tmp28_ = _tmp27_;
		_tmp29_ = g_strcmp0 (_tmp28_, "yes") == 0;
		_g_free0 (_tmp28_);
		if (_tmp29_) {
			LxsessionIconObject* _tmp30_;
			const gchar* _tmp31_;
			_tmp30_ = self->priv->crash_icon;
			_tmp31_ = remove_string;
			lxsession_icon_object_add_action (_tmp30_, "remove_crash_report", _tmp31_, _____lambda14__lxsession_icon_object_action_callback, block3_data_ref (_data3_), block3_data_unref);
		}
		_tmp32_ = self->priv->crash_icon;
		lxsession_icon_object_activate (_tmp32_);
		_g_free0 (remove_string);
		_g_free0 (launch_string);
		_g_free0 (command);
		block3_data_unref (_data3_);
		_data3_ = NULL;
	}
	self->priv->lock_crash_check = 0;
	result = FALSE;
	_g_object_unref0 (crash_menu);
	(crash_list == NULL) ? NULL : (crash_list = (_g_list_free__g_free0_ (crash_list), NULL));
	return result;
}

static void
lxsession_crash_manager_app_class_init (LxsessionCrashManagerAppClass * klass,
                                        gpointer klass_data)
{
	lxsession_crash_manager_app_parent_class = g_type_class_peek_parent (klass);
	g_type_class_adjust_private_offset (klass, &LxsessionCrashManagerApp_private_offset);
	((LxsessionAppObjectClass *) klass)->read_settings = (void (*) (LxsessionAppObject*)) lxsession_crash_manager_app_real_read_settings;
	G_OBJECT_CLASS (klass)->finalize = lxsession_crash_manager_app_finalize;
}

static void
lxsession_crash_manager_app_instance_init (LxsessionCrashManagerApp * self,
                                           gpointer klass)
{
	gchar* _tmp0_;
	self->priv = lxsession_crash_manager_app_get_instance_private (self);
	_tmp0_ = g_strdup ("/var/crash/");
	self->priv->crash_dir_path = _tmp0_;
	self->priv->lock_crash_check = 0;
}

static void
lxsession_crash_manager_app_finalize (GObject * obj)
{
	LxsessionCrashManagerApp * self;
	self = G_TYPE_CHECK_INSTANCE_CAST (obj, LXSESSION_TYPE_CRASH_MANAGER_APP, LxsessionCrashManagerApp);
	_g_free0 (self->priv->crash_manager_command);
	_g_object_unref0 (self->priv->crash_icon);
	_g_free0 (self->priv->crash_dir_path);
	_g_object_unref0 (self->priv->crash_dir_file);
	_g_object_unref0 (self->priv->crash_dir_monitor);
	G_OBJECT_CLASS (lxsession_crash_manager_app_parent_class)->finalize (obj);
}

static GType
lxsession_crash_manager_app_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (LxsessionCrashManagerAppClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) lxsession_crash_manager_app_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (LxsessionCrashManagerApp), 0, (GInstanceInitFunc) lxsession_crash_manager_app_instance_init, NULL };
	GType lxsession_crash_manager_app_type_id;
	lxsession_crash_manager_app_type_id = g_type_register_static (LXSESSION_TYPE_SIMPLE_APP_OBJECT, "LxsessionCrashManagerApp", &g_define_type_info, 0);
	LxsessionCrashManagerApp_private_offset = g_type_add_instance_private (lxsession_crash_manager_app_type_id, sizeof (LxsessionCrashManagerAppPrivate));
	return lxsession_crash_manager_app_type_id;
}

GType
lxsession_crash_manager_app_get_type (void)
{
	static volatile gsize lxsession_crash_manager_app_type_id__once = 0;
	if (g_once_init_enter (&lxsession_crash_manager_app_type_id__once)) {
		GType lxsession_crash_manager_app_type_id;
		lxsession_crash_manager_app_type_id = lxsession_crash_manager_app_get_type_once ();
		g_once_init_leave (&lxsession_crash_manager_app_type_id__once, lxsession_crash_manager_app_type_id);
	}
	return lxsession_crash_manager_app_type_id__once;
}

static void
_vala_array_destroy (gpointer array,
                     gssize array_length,
                     GDestroyNotify destroy_func)
{
	if ((array != NULL) && (destroy_func != NULL)) {
		gssize i;
		for (i = 0; i < array_length; i = i + 1) {
			if (((gpointer*) array)[i] != NULL) {
				destroy_func (((gpointer*) array)[i]);
			}
		}
	}
}

static void
_vala_array_free (gpointer array,
                  gssize array_length,
                  GDestroyNotify destroy_func)
{
	_vala_array_destroy (array, array_length, destroy_func);
	g_free (array);
}

static gssize
_vala_array_length (gpointer array)
{
	gssize length;
	length = 0;
	if (array) {
		while (((gpointer*) array)[length]) {
			length++;
		}
	}
	return length;
}
