/*
 * Copyright © 2001 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Red Hat not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Red Hat makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL RED HAT
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN 
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Owen Taylor, Red Hat, Inc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <X11/Xmd.h>		/* For CARD16 */

#include "xsettings-manager.h"

struct _XSettingsManager
{
  Display *display;
  int screen;

  Window window;
  Atom manager_atom;
  Atom selection_atom;
  Atom xsettings_atom;

  XSettingsTerminateFunc terminate;
  void *cb_data;

  XSettingsList *settings;
  unsigned long serial;
};


typedef struct 
{
  Window window;
  Atom timestamp_prop_atom;
} TimeStampInfo;

static Bool
timestamp_predicate (Display *display,
		     XEvent  *xevent,
		     XPointer arg)
{
  TimeStampInfo *info = (TimeStampInfo *)arg;

  if (xevent->type == PropertyNotify &&
      xevent->xproperty.window == info->window &&
      xevent->xproperty.atom == info->timestamp_prop_atom)
    return True;

  return False;
}

/**
 * get_server_time:
 * @display: display from which to get the time
 * @window: a #Window, used for communication with the server.
 *          The window must have PropertyChangeMask in its
 *          events mask or a hang will result.
 * 
 * Routine to get the current X server time stamp. 
 * 
 * Return value: the time stamp.
 **/
static Time
get_server_time (Display *display,
		 Window   window)
{
  unsigned char c = 'a';
  XEvent xevent;
  TimeStampInfo info;

  info.timestamp_prop_atom = XInternAtom  (display, "_TIMESTAMP_PROP", False);
  info.window = window;

  XChangeProperty (display, window,
		   info.timestamp_prop_atom, info.timestamp_prop_atom,
		   8, PropModeReplace, &c, 1);

  XIfEvent (display, &xevent,
	    timestamp_predicate, (XPointer)&info);

  return xevent.xproperty.time;
}

Bool
xsettings_manager_check_running (Display *display,
				 int      screen)
{
  char buffer[256];
  Atom selection_atom;
  
  if (display == NULL || screen < 0)
    return False;
  snprintf (buffer, sizeof buffer, "_XSETTINGS_S%d", screen);
  selection_atom = XInternAtom (display, buffer, False);

  if (XGetSelectionOwner (display, selection_atom))
    return True;
  else
    return False;
}

XSettingsManager *
xsettings_manager_new (Display                *display,
		       int                     screen,
		       XSettingsTerminateFunc  terminate,
		       void                   *cb_data)
{
  XSettingsManager *manager;
  Time timestamp;
  XClientMessageEvent xev;

  char buffer[256];

  if (display == NULL || screen < 0 || screen >= ScreenCount(display) ||
      terminate == NULL)
    return NULL;
  manager = calloc (1, sizeof *manager);
  if (!manager)
    return NULL;

  manager->display = display;
  manager->screen = screen;

  snprintf(buffer, sizeof buffer, "_XSETTINGS_S%d", screen);
  manager->selection_atom = XInternAtom (display, buffer, False);
  manager->xsettings_atom = XInternAtom (display, "_XSETTINGS_SETTINGS", False);
  manager->manager_atom = XInternAtom (display, "MANAGER", False);

  manager->terminate = terminate;
  manager->cb_data = cb_data;

  manager->settings = NULL;
  manager->serial = 0;

  manager->window = XCreateSimpleWindow (display,
					 RootWindow (display, screen),
					 0, 0, 10, 10, 0,
					 WhitePixel (display, screen),
					 WhitePixel (display, screen));

  XSelectInput (display, manager->window, PropertyChangeMask);
  timestamp = get_server_time (display, manager->window);

  XSetSelectionOwner (display, manager->selection_atom,
		      manager->window, timestamp);

  /* Check to see if we managed to claim the selection. If not,
   * we treat it as if we got it then immediately lost it
   */

  if (XGetSelectionOwner (display, manager->selection_atom) ==
      manager->window)
    {
      xev.type = ClientMessage;
      xev.window = RootWindow (display, screen);
      xev.message_type = manager->manager_atom;
      xev.format = 32;
      xev.data.l[0] = timestamp;
      xev.data.l[1] = manager->selection_atom;
      xev.data.l[2] = manager->window;
      xev.data.l[3] = 0;	/* manager specific data */
      xev.data.l[4] = 0;	/* manager specific data */
      
      XSendEvent (display, RootWindow (display, screen),
		  False, StructureNotifyMask, (XEvent *)&xev);
    }
  else
    {
      XDestroyWindow (display, manager->window);
      free (manager);
      return NULL;
    }

  return manager;
}

void
xsettings_manager_destroy (XSettingsManager *manager)
{
  if (manager == NULL)
    return;
  if (manager->display != NULL)
  {
    if (XGetSelectionOwner(manager->display, manager->selection_atom) == manager->window)
      XSetSelectionOwner(manager->display, manager->selection_atom, None, CurrentTime);
    XDestroyWindow(manager->display, manager->window);
  }
  xsettings_list_free(manager->settings);
  free(manager);
}

Window
xsettings_manager_get_window (XSettingsManager *manager)
{
  return manager != NULL ? manager->window : None;
}

Bool
xsettings_manager_process_event (XSettingsManager *manager,
				 XEvent           *xev)
{
  if (manager == NULL || xev == NULL)
    return False;
  if (xev->xany.window == manager->window &&
      xev->xany.type == SelectionClear &&
      xev->xselectionclear.selection == manager->selection_atom)
    {
      manager->terminate (manager->cb_data);
      return True;
    }

  return False;
}

XSettingsResult
xsettings_manager_delete_setting (XSettingsManager *manager,
                                  const char       *name)
{
  if (manager == NULL)
    return XSETTINGS_FAILED;
  return xsettings_list_delete (&manager->settings, name);
}

XSettingsResult
xsettings_manager_set_setting (XSettingsManager *manager,
			       XSettingsSetting *setting)
{
  XSettingsSetting *old_setting;
  if (manager == NULL || setting == NULL || setting->name == NULL ||
      (setting->type != XSETTINGS_TYPE_INT &&
       setting->type != XSETTINGS_TYPE_STRING &&
       setting->type != XSETTINGS_TYPE_COLOR) ||
      strnlen(setting->name, XSETTINGS_MAX_NAME + 1U) > XSETTINGS_MAX_NAME ||
      (setting->type == XSETTINGS_TYPE_STRING &&
       (setting->data.v_string == NULL ||
        strnlen(setting->data.v_string, XSETTINGS_MAX_STRING + 1U) > XSETTINGS_MAX_STRING)))
    return XSETTINGS_FAILED;
  old_setting = xsettings_list_lookup (manager->settings, setting->name);
  XSettingsSetting *new_setting;
  XSettingsResult result;

  if (old_setting)
    {
      if (xsettings_setting_equal (old_setting, setting))
	return XSETTINGS_SUCCESS;

      xsettings_list_delete (&manager->settings, setting->name);
    }

  new_setting = xsettings_setting_copy (setting);
  if (!new_setting)
    return XSETTINGS_NO_MEM;
  
  new_setting->last_change_serial = manager->serial;
  
  result = xsettings_list_insert (&manager->settings, new_setting);
  
  if (result != XSETTINGS_SUCCESS)
    xsettings_setting_free (new_setting);

  return result;
}

XSettingsResult
xsettings_manager_set_int (XSettingsManager *manager,
			   const char       *name,
			   int               value)
{
  XSettingsSetting setting;
  if (manager == NULL || name == NULL)
    return XSETTINGS_FAILED;

  setting.name = (char *)name;
  setting.type = XSETTINGS_TYPE_INT;
  setting.data.v_int = value;

  return xsettings_manager_set_setting (manager, &setting);
}

XSettingsResult
xsettings_manager_set_string (XSettingsManager *manager,
			      const char       *name,
			      const char       *value)
{
  XSettingsSetting setting;
  if (manager == NULL || name == NULL || value == NULL ||
      strnlen(name, XSETTINGS_MAX_NAME + 1U) > XSETTINGS_MAX_NAME ||
      strnlen(value, XSETTINGS_MAX_STRING + 1U) > XSETTINGS_MAX_STRING)
    return XSETTINGS_FAILED;

  setting.name = (char *)name;
  setting.type = XSETTINGS_TYPE_STRING;
  setting.data.v_string = (char *)value;

  return xsettings_manager_set_setting (manager, &setting);
}

XSettingsResult
xsettings_manager_set_color (XSettingsManager *manager,
			     const char       *name,
			     XSettingsColor   *value)
{
  XSettingsSetting setting;
  if (manager == NULL || name == NULL || value == NULL ||
      strnlen(name, XSETTINGS_MAX_NAME + 1U) > XSETTINGS_MAX_NAME)
    return XSETTINGS_FAILED;

  setting.name = (char *)name;
  setting.type = XSETTINGS_TYPE_COLOR;
  setting.data.v_color = *value;

  return xsettings_manager_set_setting (manager, &setting);
}

static size_t
setting_length (XSettingsSetting *setting)
{
  size_t length = 8;	/* type + pad + name-len + last-change-serial */
  size_t name_len;
  size_t value_len = 0;

  if (setting == NULL || setting->name == NULL ||
      (name_len = strnlen(setting->name, XSETTINGS_MAX_NAME + 1U)) > XSETTINGS_MAX_NAME)
    return 0;
  if (setting->type == XSETTINGS_TYPE_STRING)
  {
    if (setting->data.v_string == NULL ||
        (value_len = strnlen(setting->data.v_string, XSETTINGS_MAX_STRING + 1U)) > XSETTINGS_MAX_STRING)
      return 0;
  }
  if (length > SIZE_MAX - XSETTINGS_PAD(name_len, 4))
    return 0;
  length += XSETTINGS_PAD (name_len, 4);

  switch (setting->type)
    {
    case XSETTINGS_TYPE_INT:
      length += 4;
      break;
    case XSETTINGS_TYPE_STRING:
      if (length > SIZE_MAX - 4U - XSETTINGS_PAD(value_len, 4))
        return 0;
      length += 4 + XSETTINGS_PAD (value_len, 4);
      break;
    case XSETTINGS_TYPE_COLOR:
      length += 8;
      break;
    }

  return length;
}

static void
setting_store (XSettingsSetting *setting,
	       XSettingsBuffer *buffer)
{
  size_t string_len;
  size_t length;

  *(buffer->pos++) = setting->type;
  *(buffer->pos++) = 0;

  string_len = strlen (setting->name);
  *(CARD16 *)(buffer->pos) = string_len;
  buffer->pos += 2;

  length = XSETTINGS_PAD (string_len, 4);
  memcpy (buffer->pos, setting->name, string_len);
  length -= string_len;
  buffer->pos += string_len;
  
  while (length > 0)
    {
      *(buffer->pos++) = 0;
      length--;
    }

  *(CARD32 *)(buffer->pos) = setting->last_change_serial;
  buffer->pos += 4;

  switch (setting->type)
    {
    case XSETTINGS_TYPE_INT:
      *(CARD32 *)(buffer->pos) = setting->data.v_int;
      buffer->pos += 4;
      break;
    case XSETTINGS_TYPE_STRING:
      string_len = strlen (setting->data.v_string);
      *(CARD32 *)(buffer->pos) = string_len;
      buffer->pos += 4;
      
      length = XSETTINGS_PAD (string_len, 4);
      memcpy (buffer->pos, setting->data.v_string, string_len);
      length -= string_len;
      buffer->pos += string_len;
      
      while (length > 0)
	{
	  *(buffer->pos++) = 0;
	  length--;
	}
      break;
    case XSETTINGS_TYPE_COLOR:
      *(CARD16 *)(buffer->pos) = setting->data.v_color.red;
      *(CARD16 *)(buffer->pos + 2) = setting->data.v_color.green;
      *(CARD16 *)(buffer->pos + 4) = setting->data.v_color.blue;
      *(CARD16 *)(buffer->pos + 6) = setting->data.v_color.alpha;
      buffer->pos += 8;
      break;
    }
}

XSettingsResult
xsettings_manager_notify (XSettingsManager *manager)
{
  XSettingsBuffer buffer;
  XSettingsList *iter;
  int n_settings = 0;

  if (manager == NULL)
    return XSETTINGS_FAILED;
  buffer.len = 12;		/* byte-order + pad + SERIAL + N_SETTINGS */

  iter = manager->settings;
  while (iter)
    {
      size_t item_length = setting_length (iter->setting);
      if (item_length == 0 || n_settings >= XSETTINGS_MAX_SETTINGS ||
          buffer.len > XSETTINGS_MAX_BUFFER - item_length)
        return XSETTINGS_FAILED;
      buffer.len += item_length;
      n_settings++;
      iter = iter->next;
    }

  buffer.data = buffer.pos = malloc (buffer.len);
  if (!buffer.data)
    return XSETTINGS_NO_MEM;

  *buffer.pos = xsettings_byte_order ();

  buffer.pos += 4;
  *(CARD32 *)buffer.pos = manager->serial++;
  buffer.pos += 4;
  *(CARD32 *)buffer.pos = n_settings;
  buffer.pos += 4;

  iter = manager->settings;
  while (iter)
    {
      setting_store (iter->setting, &buffer);
      iter = iter->next;
    }

  XChangeProperty (manager->display, manager->window,
		   manager->xsettings_atom, manager->xsettings_atom,
		   8, PropModeReplace, buffer.data, buffer.len);

  free (buffer.data);

  return XSETTINGS_SUCCESS;
}

