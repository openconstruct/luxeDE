// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostic-common.hh"
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <memory>
static fs::path marker;
static GtkStatusIcon *tray;
static Display *display;
static bool failed = false;
static bool mapped() {
  XWindowAttributes attr{};
  return XGetWindowAttributes(display, gtk_status_icon_get_x11_window_id(tray),
                              &attr) &&
         attr.map_state == IsViewable;
}
static void flag(unsigned long value) {
  auto atom = XInternAtom(display, "_XEMBED_INFO", false);
  unsigned long data[] = {1, value};
  XChangeProperty(display, gtk_status_icon_get_x11_window_id(tray), atom, atom,
                  32, PropModeReplace, reinterpret_cast<unsigned char *>(data),
                  2);
  XFlush(display);
}
static gboolean tray_stage(gpointer data) {
  try {
    switch (GPOINTER_TO_INT(data)) {
    case 1:
      XUnmapWindow(display, gtk_status_icon_get_x11_window_id(tray));
      XFlush(display);
      break;
    case 2:
      require(mapped(), "Host did not restore visible plug");
      flag(0);
      break;
    case 3:
      require(!mapped(), "Host ignored cleared XEMBED_MAPPED flag");
      flag(1);
      break;
    case 4: {
      require(gtk_status_icon_is_embedded(tray), "Tray not embedded");
      require(mapped(), "Tray not visible");
      auto root = gdk_get_default_root_window();
      auto pix = gdk_pixbuf_get_from_window(root, 0, 0, 600, 160);
      require(pix, "Screenshot failed");
      bool saved =
          gdk_pixbuf_save(pix, marker.c_str(), "png", nullptr, nullptr);
      g_object_unref(pix);
      require(saved, "Screenshot save failed");
      std::cout << "PASS: unmap recovery, XEMBED_MAPPED hide/show and real "
                   "GTK3 tray embedding\n";
      gtk_main_quit();
      break;
    }
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    failed = true;
    gtk_main_quit();
  }
  return G_SOURCE_REMOVE;
}
int main(int argc, char **argv) {
  try {
    require(argc >= 2, "Usage: gtk3-fixtures windows|focus|tray [MARKER]");
    gtk_init(nullptr, nullptr);
    std::string mode = argv[1];
    if (mode == "windows") {
      for (auto name : {"Luxe Alpha", "Luxe Beta"}) {
        auto w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(w), name);
        gtk_widget_show_all(w);
      }
    } else if (mode == "compact") {
      require(argc == 3, "Missing close marker");
      marker = argv[2];
      auto w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(w), "Compact decoration fixture");
      gtk_window_set_default_size(GTK_WINDOW(w), 420, 220);
      g_signal_connect(
          w, "delete-event",
          G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer) -> gboolean {
            try {
              write_file(marker, "");
            } catch (...) {
              failed = true;
            }
            gtk_main_quit();
            return false;
          }),
          nullptr);
      gtk_container_add(
          GTK_CONTAINER(w),
          gtk_label_new(
              "Classic Openbox window — title text remains readable"));
      gtk_widget_show_all(w);
    } else if (mode == "focus") {
      require(argc == 3, "Missing entry marker");
      marker = argv[2];
      auto w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(w), "Menu focus fixture");
      auto entry = gtk_entry_new();
      g_signal_connect(entry, "changed",
                       G_CALLBACK(+[](GtkEntry *entry, gpointer) {
                         try {
                           write_file(marker, gtk_entry_get_text(entry));
                         } catch (const std::exception &e) {
                           std::cerr << e.what() << '\n';
                           failed = true;
                           gtk_main_quit();
                         }
                       }),
                       nullptr);
      gtk_container_add(GTK_CONTAINER(w), entry);
      gtk_widget_show_all(w);
    } else if (mode == "tray") {
      require(argc == 3, "Missing screenshot path");
      marker = argv[2];
      tray = gtk_status_icon_new_from_icon_name("network-wireless");
      gtk_status_icon_set_title(tray, "Luxe tray regression");
      gtk_status_icon_set_visible(tray, true);
      auto w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_default_size(GTK_WINDOW(w), 250, 70);
      gtk_widget_show_all(w);
      display = XOpenDisplay(nullptr);
      require(display, "No X display");
      g_timeout_add(
          700,
          +[](gpointer p) -> gboolean {
            gtk_window_set_title(GTK_WINDOW(p), "Luxe title updated");
            return G_SOURCE_REMOVE;
          },
          w);
      for (auto [ms, stage] : {std::pair{900, 1}, std::pair{1250, 2},
                               std::pair{1600, 3}, std::pair{2300, 4}})
        g_timeout_add(ms, tray_stage, GINT_TO_POINTER(stage));
    } else
      throw std::runtime_error("Unknown GTK3 fixture");
    gtk_main();
    if (display)
      XCloseDisplay(display);
    return failed ? 1 : 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
