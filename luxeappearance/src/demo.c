/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>

#include "demo.h"
#include "demo-ui.h"
#include "main-dlg.h"
#include "glade-support.h"

static GtkFlowBox* icon_view = NULL;
static GtkWidget* toolbar = NULL;

/* Toolbar style: 0 = icons only, 1 = text only, 2 = text below icons,
 * 3 = text beside icons. Mirrors the tb_style combo in the main dialog. */
static void fill_toolbar(int tb_style)
{
    static const struct { const char* icon; const char* text; const char* tip; } tools[] = {
        { "go-previous",  N_("Back"),    N_("Back") },
        { "go-next",      N_("Forward"), N_("Forward") },
        { "process-stop", N_("Stop"),    N_("Stop") }
    };
    unsigned int i;
    GtkWidget* child;

    if(!toolbar)
        return;

    while((child = gtk_widget_get_first_child(toolbar)) != NULL)
        gtk_box_remove(GTK_BOX(toolbar), child);

    for(i = 0; i < G_N_ELEMENTS(tools); ++i)
    {
        GtkWidget* btn = gtk_button_new();
        gtk_widget_set_tooltip_text(btn, g_dgettext(GETTEXT_PACKAGE, tools[i].tip));
        if(tb_style == 0) /* icons only */
        {
            gtk_button_set_icon_name(GTK_BUTTON(btn), tools[i].icon);
        }
        else if(tb_style == 1) /* text only */
        {
            gtk_button_set_label(GTK_BUTTON(btn), g_dgettext(GETTEXT_PACKAGE, tools[i].text));
        }
        else
        {
            GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            GtkWidget* img;
            GtkWidget* label;
            if(tb_style == 3) /* text beside icons */
                gtk_orientable_set_orientation(GTK_ORIENTABLE(box), GTK_ORIENTATION_HORIZONTAL);
            img = gtk_image_new_from_icon_name(tools[i].icon);
            label = gtk_label_new(g_dgettext(GETTEXT_PACKAGE, tools[i].text));
            gtk_box_append(GTK_BOX(box), img);
            gtk_box_append(GTK_BOX(box), label);
            gtk_button_set_child(GTK_BUTTON(btn), box);
        }
        gtk_box_append(GTK_BOX(toolbar), btn);
    }
}

void demo_refresh_toolbar( void )
{
    fill_toolbar(main_dlg_get_tb_style());
}

static void load_demo_icons(GtkIconTheme* theme, gpointer data)
{
    static const char* icon_names[]={
        "user-home",
        "user-desktop",
        "folder",
        "user-trash",
        "text-x-generic",
        "application-x-executable",
        "image-x-generic",
        "text-x-generic"
    };

    unsigned int i;
    GtkWidget* child;

    (void)data;
    if(!icon_view)
        return;
    if(!theme)
        theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    if(!theme)
        return;

    while((child = gtk_widget_get_first_child(GTK_WIDGET(icon_view))) != NULL)
        gtk_flow_box_remove(icon_view, child);

    for( i = 0; i < G_N_ELEMENTS(icon_names); ++i )
    {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget* img;
        GtkWidget* label;
        GtkIconPaintable* icon = gtk_icon_theme_lookup_icon(theme,
                                                            icon_names[i],
                                                            NULL, 32, 1,
                                                            GTK_TEXT_DIR_LTR, 0);
        if(icon)
        {
            img = gtk_image_new_from_paintable(GDK_PAINTABLE(icon));
            g_object_unref(icon);
        }
        else
            img = gtk_image_new_from_icon_name("image-missing");
        label = gtk_label_new(icon_names[i]);
        gtk_box_append(GTK_BOX(box), img);
        gtk_box_append(GTK_BOX(box), label);
        gtk_flow_box_append(icon_view, box);
    }
}

static void load_demo_tree_view( GtkTreeView* view )
{
    GtkListStore* list;
    GtkTreeViewColumn* col;
    int i;
    char str[256];

    for( i = 0; i < 3; ++i )
    {
        g_snprintf( str, sizeof(str), "%s %d", _("Column"), i + 1 );
        col = gtk_tree_view_column_new_with_attributes( str, gtk_cell_renderer_text_new(), "text", i, NULL );
        gtk_tree_view_append_column( view, col );
    }

    list = gtk_list_store_new( 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING );
    for( i = 0; i < 3; ++i )
    {
        GtkTreeIter it;
        g_snprintf( str, sizeof(str), "%s %d", _("Item"), i + 1 );
        gtk_list_store_append( list, &it );
        gtk_list_store_set( list, &it, 0, str, 1, str, 2, str, -1 );
    }
    gtk_tree_view_set_model( view, (GtkTreeModel*)list );
    g_object_unref( list );
}

/* Demo menu items do nothing; the menubar exists so themes can preview
 * menu styling. */
static void on_demo_action(GSimpleAction* action, GVariant* param, gpointer data)
{
    (void)action; (void)param; (void)data;
}

static void setup_demo_actions(GtkWidget* root)
{
    static const char* names[] = {
        "new", "open", "save", "quit", "cut", "copy", "paste", "about"
    };
    unsigned int i;
    GSimpleActionGroup* group = g_simple_action_group_new();
    for(i = 0; i < G_N_ELEMENTS(names); ++i)
    {
        GSimpleAction* act = g_simple_action_new(names[i], NULL);
        g_signal_connect(act, "activate", G_CALLBACK(on_demo_action), NULL);
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
        g_object_unref(act);
    }
    gtk_widget_insert_action_group(root, "demo", G_ACTION_GROUP(group));
    g_object_unref(group);
}

GtkWidget* show_demo( void )
{
    GtkWidget* demo = create_demo_window();
    GtkWidget* tree_view;
    GtkWidget* text_view;
    GtkTextBuffer* buf;

    if(!demo)
        return NULL;

    setup_demo_actions(demo);

    toolbar = GTK_WIDGET(lookup_widget(demo, "toolbar"));
    icon_view = GTK_FLOW_BOX(lookup_widget(demo, "icon_view"));
    tree_view = lookup_widget(demo, "demo_treeview");
    text_view = lookup_widget(demo, "text_view");

    fill_toolbar(main_dlg_get_tb_style());

    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buf,
        _("Type some characters here to test currently selected font."), -1);

    load_demo_icons(NULL, NULL);
    load_demo_tree_view(GTK_TREE_VIEW(tree_view));

    /* The handler lives as long as this preview: GtkIconTheme is a
     * process-wide singleton, so an untracked connection kept the destroyed
     * demo alive and fired once per window that had ever been created. */
    g_signal_connect_object(gtk_icon_theme_get_for_display(gdk_display_get_default()),
                            "changed", G_CALLBACK(load_demo_icons), demo, 0);

    return demo;
}
