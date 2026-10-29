// Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later
#include "diagnostic-common.hh"
#include <gtk/gtk.h>
#ifdef WITH_ADWAITA
#include <adwaita.h>
#endif
static void settle(int ms) {
  auto loop = g_main_loop_new(nullptr, false);
  g_timeout_add(
      ms,
      +[](gpointer p) -> gboolean {
        g_main_loop_quit(static_cast<GMainLoop *>(p));
        return G_SOURCE_REMOVE;
      },
      loop);
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
}
static void title(GtkWidget *header, GtkWidget *label) {
#ifdef WITH_ADWAITA
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), label);
#elif GTK_MAJOR_VERSION == 3
  gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header), label);
  if (label)
    gtk_widget_show(label);
#else
  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), label);
#endif
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "Usage: theme-widget-fixture THEME_BRIDGE");
    if (std::string(argv[1]) == "--minor") {
      std::cout << gtk_get_minor_version() << "\n";
      return 0;
    }
    fs::path theme = fs::absolute(argv[1]),
             root = theme.parent_path().parent_path();
    auto meta = g_key_file_new();
    require(g_key_file_load_from_file(meta, (theme / "metadata.ini").c_str(),
                                      G_KEY_FILE_NONE, nullptr),
            "Missing theme metadata");
    auto field = [&](const char *key) {
      auto p = g_key_file_get_string(meta, "Theme", key, nullptr);
      require(p, "Missing metadata field");
      std::string out = p;
      g_free(p);
      return out;
    };
    auto name = field("name");
#ifdef WITH_ADWAITA
    const char *toolkit = "adw";
    adw_init();
    adw_style_manager_set_color_scheme(
        adw_style_manager_get_default(),
        g_key_file_get_boolean(meta, "Theme", "dark", nullptr)
            ? ADW_COLOR_SCHEME_FORCE_DARK
            : ADW_COLOR_SCHEME_FORCE_LIGHT);
#elif GTK_MAJOR_VERSION == 3
    const char *toolkit = "gtk3";
    gtk_init(nullptr, nullptr);
#else
    const char *toolkit = "gtk4";
    gtk_init();
#endif
    auto provider = gtk_css_provider_new();
    bool errors = false;
    g_signal_connect(provider, "parsing-error",
                     G_CALLBACK(+[](GtkCssProvider *, GtkCssSection *,
                                    const GError *e, gpointer p) {
                       *static_cast<bool *>(p) = true;
                       std::cerr << e->message << '\n';
                     }),
                     &errors);
    std::vector<std::string> versions;
#if GTK_MAJOR_VERSION == 3
    versions = {"gtk-3.0"};
#else
    versions = {"gtk-4.0"};
    if (gtk_get_minor_version() >= 16)
      versions.push_back("gtk-4.16");
#endif
    for (auto &v : versions) {
      auto path = root / "themes" / ("Luxe-" + theme.filename().string()) / v /
                  "gtk.css";
#if GTK_MAJOR_VERSION == 3
      gtk_css_provider_load_from_path(provider, path.c_str(), nullptr);
#else
      gtk_css_provider_load_from_path(provider, path.c_str());
#endif
    }
    require(!errors, "Stylesheet parsing errors");
    g_object_unref(provider);
    GtkWidget *window, *header;
#ifdef WITH_ADWAITA
    window = adw_application_window_new(nullptr);
    header = adw_header_bar_new();
#else
#if GTK_MAJOR_VERSION == 3
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#else
    window = gtk_window_new();
#endif
    header = gtk_header_bar_new();
#endif
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 470);
    gtk_window_set_title(GTK_WINDOW(window), (name + " — " + toolkit).c_str());
    g_object_set(gtk_settings_get_default(), "gtk-font-name", "Sans 10",
                 "gtk-decoration-layout", "menu:minimize,maximize,close",
                 nullptr);
#if GTK_MAJOR_VERSION == 3
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), true);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), name.c_str());
#else
    title(header, gtk_label_new(name.c_str()));
#endif
    auto menu = gtk_menu_button_new();
    GtkWidget *popover;
#if GTK_MAJOR_VERSION == 3
    gtk_container_add(GTK_CONTAINER(menu),
                      gtk_image_new_from_icon_name("open-menu-symbolic",
                                                   GTK_ICON_SIZE_BUTTON));
    popover = gtk_popover_new(nullptr);
#else
    gtk_menu_button_set_child(
        GTK_MENU_BUTTON(menu),
        gtk_image_new_from_icon_name("open-menu-symbolic"));
    popover = gtk_popover_new();
#endif
    auto action = gtk_button_new_with_label("Header action");
#if GTK_MAJOR_VERSION == 3
    gtk_container_add(GTK_CONTAINER(popover), action);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu), popover);
#else
    gtk_popover_set_child(GTK_POPOVER(popover), action);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu), popover);
#endif
#ifdef WITH_ADWAITA
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu);
#else
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu);
#endif
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 14);
    gtk_widget_set_margin_end(box, 14);
    auto label = gtk_label_new("A classic desktop. A cohesive palette."),
         entry = gtk_entry_new(),
         normal = gtk_button_new_with_label("Ordinary action"),
         primary = gtk_button_new_with_label("Primary action"),
         danger = gtk_button_new_with_label("Destructive action"),
         disabled = gtk_button_new_with_label("Unavailable"),
         check = gtk_check_button_new_with_label("Check button"),
         bar = gtk_progress_bar_new(),
         slider =
             gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
#if GTK_MAJOR_VERSION == 3
    gtk_entry_set_text(GTK_ENTRY(entry), "Entry and selection");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), true);
    gtk_style_context_add_class(gtk_widget_get_style_context(primary),
                                "suggested-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(danger),
                                "destructive-action");
#else
    gtk_editable_set_text(GTK_EDITABLE(entry), "Entry and selection");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), true);
    gtk_widget_add_css_class(primary, "suggested-action");
    gtk_widget_add_css_class(danger, "destructive-action");
#endif
    gtk_widget_set_sensitive(disabled, false);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), .55);
    gtk_range_set_value(GTK_RANGE(slider), 60);
    for (auto widget : {label, entry, normal, primary, danger, disabled, check,
                        bar, slider}) {
#if GTK_MAJOR_VERSION == 3
      gtk_box_pack_start(GTK_BOX(box), widget, false, false, 0);
#else
      gtk_box_append(GTK_BOX(box), widget);
#endif
    }
#ifdef WITH_ADWAITA
    auto toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), box);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), toolbar);
#elif GTK_MAJOR_VERSION == 3
    gtk_container_add(GTK_CONTAINER(window), box);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
#else
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
    gtk_window_set_child(GTK_WINDOW(window), box);
#endif
#if GTK_MAJOR_VERSION == 3
    gtk_widget_show_all(window);
#else
    gtk_window_present(GTK_WINDOW(window));
#endif
    settle(200);
    int height = gtk_widget_get_allocated_height(header);
    require(height >= 24 && height <= 34,
            std::string(toolkit) + " header height=" + std::to_string(height));
    require(gtk_widget_get_allocated_height(normal) >= 28,
            "Body button shrank");
#if GTK_MAJOR_VERSION == 3
    gtk_widget_show_all(popover);
#else
    gtk_menu_button_popup(GTK_MENU_BUTTON(menu));
#endif
    settle(120);
    require(gtk_widget_get_mapped(popover), "Header menu did not open");
    require(gtk_widget_get_allocated_height(action) >= 28,
            "Popover inherited compact sizing");
    gtk_popover_popdown(GTK_POPOVER(popover));
#if GTK_MAJOR_VERSION == 3
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(header), false);
    settle(160);
    require(gtk_widget_get_allocated_height(header) >= 24 &&
                gtk_widget_get_allocated_height(header) <= 34,
            "Header without subtitle size");
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(header), true);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header),
                                "A useful subtitle must remain visible");
    settle(160);
    require(gtk_widget_get_allocated_height(header) >= height,
            "Subtitle clipped");
#endif
    auto large = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(large),
                         "<span size=\"28000\">Readable large title</span>");
    title(header, large);
    settle(180);
    require(gtk_widget_get_allocated_height(header) > height,
            "Large title clipped");
#if GTK_MAJOR_VERSION == 3
    title(header, nullptr);
#else
    title(header, gtk_label_new(name.c_str()));
#endif
    settle(180);
    auto contrast = [&](GtkWidget *widget, const char *key) {
      GdkRGBA expected{}, actual{};
      require(gdk_rgba_parse(&expected, field(key).c_str()),
              "Invalid palette color");
#if GTK_MAJOR_VERSION == 3
      gtk_style_context_get_color(gtk_widget_get_style_context(widget),
                                  GTK_STATE_FLAG_NORMAL, &actual);
#else
      gtk_style_context_get_color(gtk_widget_get_style_context(widget),
                                  &actual);
#endif
      require(std::abs(actual.red - expected.red) < .015 &&
                  std::abs(actual.green - expected.green) < .015 &&
                  std::abs(actual.blue - expected.blue) < .015,
              "Wrong foreground: " + std::string(toolkit) + " " + name + " " +
                  key);
    };
    contrast(label, "fg");
    contrast(primary, "on-accent");
#ifdef WITH_ADWAITA
    if (auto output = getenv("LUXE_THEME_SCREENSHOTS")) {
      fs::create_directories(output);
      command(
          {"import", "-window", "root",
           (fs::path(output) / (theme.filename().string() + ".png")).string()},
          {}, 6);
    }
#endif
#if GTK_MAJOR_VERSION == 3
    gtk_widget_destroy(window);
#else
    gtk_window_destroy(GTK_WINDOW(window));
#endif
    g_key_file_unref(meta);
    std::cout << "PASS: " << toolkit << ' ' << name
              << " compact header/menu/body sizing, large title and foreground "
                 "contrast\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
