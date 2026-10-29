/* 
    Copyright 2013 Julien Lavergne <gilir@ubuntu.com>
    Copyright (C) 2026 Jerry Howell

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
using Gtk;

namespace LDefaultApps
{
    [CCode (cname = "lxsession_set_mime_defaults", cheader_filename = "mime-defaults.h")]
    private bool set_mime_defaults (string[] types, string desktop_id) throws GLib.Error;
    [CCode (cname = "lxsession_key_file_load_bounded")]
    private bool load_key_file_bounded (KeyFile key_file, string path)
        throws GLib.Error;
    [CCode (cname = "lxsession_atomic_write_file")]
    private bool atomic_write_file (string path, string contents, ssize_t length)
        throws GLib.Error;
    void init_application(   Builder builder,
                                    KeyFile kf,
                                    DbusBackend dbus_backend,
                                    string key1,
                                    string key2,
                                    string message_help,
                                    string[] more_list,
                                    string more_help,
                                    string? generic_item)
    {
        init_entry(builder, dbus_backend, key1);
        init_reload_button(builder, dbus_backend, key1, key2);
        init_help_message(builder, key1, message_help);
        init_more_button(builder, dbus_backend, kf, key1, more_list, more_help, generic_item);
    }

    void init_application_combobox ( Gtk.Builder builder,
                                            KeyFile kf,
                                            DbusBackend dbus_backend,
                                            string key1,
                                            string key2,
                                            string message_help,
                                            string[] more_list,
                                            string more_help,
                                            string? generic_item)
    {
        init_combobox(builder, dbus_backend, kf, key1 + "_combobox", key1, dbus_backend.Get(key1, "command"), generic_item);
        init_reload_button(builder, dbus_backend, key1, key2);
        init_help_message(builder, key1, message_help);
        init_more_button(builder, dbus_backend, kf, key1, more_list, more_help, generic_item);
    }

    void init_entry(Builder builder, DbusBackend dbus_backend, string key1)
    {
        string default_text = dbus_backend.Get(key1, "command");
        string entry_name = key1 + "_entry";
        var entry = builder.get_object (entry_name) as Entry;
        entry.text = default_text;
        entry.changed.connect(() => {
            if (entry.text != null)
            {
                dbus_backend.Set(key1, "command", entry.text);
            }
        });
        entry.show();
    }

    void init_reload_button(Builder builder, DbusBackend dbus_backend, string key1, string key2)
    {
        var button = builder.get_object(key1 + "_reload") as Gtk.Button;
        button.clicked.connect (() => {
            dbus_backend.Launch(key1, key2);
        });
    }

    void init_help_message(Builder builder, string item, string message_label)
    {
        var help_button = builder.get_object(item + "_help_button") as Gtk.Button;
        help_button.clicked.connect (() => {
            var help_window = new Window();
            help_window.set_default_size (350, 70);
            help_window.icon_name = "preferences-desktop";

            var help_label = new Label(message_label);
            help_window.set_child (help_label);
            help_window.present();
        });
    }

    void init_more_button(Builder builder, DbusBackend dbus_backend, KeyFile kf, string item, string[] more_list, string more_help, string generic_item)
    {
        /* Copy the array, it doesn't seem to like to be passed directly */
        string[] more_list_copy = more_list;
        var button = builder.get_object(item + "_more") as Gtk.Button;
        button.clicked.connect (() => {
            init_windows_more(builder, dbus_backend, kf, item, more_list_copy, more_help, generic_item);
        });
    }

    void init_windows_more (Builder builder, DbusBackend dbus_backend, KeyFile kf, string item, string[] more_list, string message_help, string generic_item)
    {
        var window = new Gtk.Window();
        window.set_default_size (300, 500);
        window.icon_name = "preferences-desktop";


        // The ScrolledWindow:
		Gtk.ScrolledWindow more_scrolled = new Gtk.ScrolledWindow ();
        more_scrolled.set_policy (PolicyType.NEVER, PolicyType.AUTOMATIC);
		window.set_child (more_scrolled);

        var more_view_port = new Gtk.Viewport(null, null);
        more_scrolled.set_child (more_view_port);

        var master_vbox = new Gtk.Box (Gtk.Orientation.VERTICAL, 0);
        more_view_port.set_child (master_vbox);

        var help_message = new Label(message_help);
        master_vbox.append (help_message);
        for (int a = 0 ; a < more_list.length ; a++)
        {
            switch (more_list[a])
            {
                case "combobox_manual":
                    var box = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
                    master_vbox.append (box);

                    var label = new Label(_("Manual setting"));
                    box.append (label);

                    string default_text = dbus_backend.Get(item, "command");
                    var manual_entry = new Gtk.Entry();
                    manual_entry.text = default_text;
                    manual_entry.changed.connect(() => {
                        if (manual_entry.text != null)
                        {
                            dbus_backend.Set(item, "command", manual_entry.text);
                            /* TODO update the main combobox */
                        }
                    });
                    box.append (manual_entry);
                    break;

                case "mime_association":
                    var box = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
                    master_vbox.append (box);

                    var label = new Label(_("Mime Association"));
                    box.append (label);

                    var button = new Gtk.Button.with_label(_("Apply"));
  
                    button.clicked.connect(() => {
                        string[] combobox_list;
                        combobox_list = get_mime_list(kf, item, "installed");
                        string default_path = "";
                        string default_exec = "";

                        for (int b = 0 ; b < combobox_list.length ; b++)
                        {
                            string item_list = combobox_list[b];
                            ComboItemObject combo_item = new ComboItemObject (item_list);
                            if (item == combo_item.exec)
                            {
                                default_path = combo_item.desktop_path;
                                default_exec = combo_item.exec;
                            }
                        }
                        create_mime_associate_window(dbus_backend, kf, item, default_exec);
                    });
                    box.append (button);
                    break;
                case "mime_available":
                    var box = new Gtk.Box (Gtk.Orientation.VERTICAL, 0);
                    master_vbox.append (box);

                    var label = new Label(_("Available applications"));
                    box.append (label);

                    Gtk.ListStore list_store = new Gtk.ListStore (3, typeof(string), typeof(string), typeof(string));
                    Gtk.TreeIter iter;

                    string[] list ;
                    if (generic_item != null)
                    {
                        list = get_mime_list(kf, generic_item, "available");
                    }
                    else
                    {
                        list = get_mime_list(kf, item, "available");
                    }

                    for (int c = 0 ; c < list.length ; c++)
                    {
                            string item_list = list[c];
                            ComboItemObject combo_item = new ComboItemObject (item_list);

                            list_store.append (out iter);
                            list_store.set (iter, 0, combo_item.icon_name , 1, combo_item.name, 2, "gtk-apply");
                    }

                    var return_treeview = new Gtk.TreeView.with_model(list_store);
                    return_treeview.enable_search = false;

                    Gtk.CellRendererPixbuf renderer_image = new Gtk.CellRendererPixbuf ();
                    return_treeview.insert_column_with_attributes (-1, "Icon", renderer_image, "icon-name", 0);

                    Gtk.CellRendererText renderer = new Gtk.CellRendererText ();
                    return_treeview.insert_column_with_attributes (-1, "Name", renderer, "text", 1);

                    Gtk.CellRendererPixbuf renderer_apply = new Gtk.CellRendererPixbuf ();
                    return_treeview.insert_column_with_attributes (-1, "", renderer_apply, "icon-name", 2);

                    /* TODO callback the install button */

                    box.append (return_treeview);

                    break;
                case "autostart":
                    var hbox = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
                    master_vbox.append (hbox);

                    var check_button = new Gtk.CheckButton();
                    check_button.set_label(_("Autostart the application ?"));
                    string default_text = dbus_backend.Get(item, "autostart");
                    if (default_text == "true")
                    {
                        check_button.set_active(true);
                    }
                    else
                    {
                        check_button.set_active(false);
                    }
                    check_button.toggled.connect((sender) => {
                        dbus_backend.Set(item, "autostart",
                                         sender.active ? "true" : "false");
                    });
                    hbox.append (check_button);
                    break;

                case "handle_desktop":
                    var hbox = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
                    master_vbox.append (hbox);

                    var check_button = new Gtk.CheckButton();
                    check_button.set_label(_("Handle the desktop with it ?"));
                    string default_text = dbus_backend.Get("desktop_manager", "command");
                    if (default_text == "filemanager")
                    {
                        check_button.set_active(true);
                    }
                    else
                    {
                        check_button.set_active(false);
                    }
                    check_button.toggled.connect((sender) => {
                        dbus_backend.Set("desktop_manager", "command",
                                         sender.active ? "filemanager" : "");
                    });
                    hbox.append (check_button);
                    break;

                case "debian_default":
                    var box = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
                    master_vbox.append (box);

                    var label = new Label(_("Set debian default programs"));
                    box.append (label);

                    var button = new Gtk.Button.with_label(_("Apply"));
  
                    button.clicked.connect(() => {

                        string default_text = dbus_backend.Get(item, "command");
                        if (default_text == null || default_text == "")
                            return;
                        string default_command = default_text.has_prefix("/") ?
                            default_text : "/usr/bin/" + default_text;
                        string group = null;
                        if (item == "webbrowser")
                            group = "x-www-browser";
                        else if (item == "terminal_manager")
                            group = "x-terminal-emulator";
                        if (group == null || !FileUtils.test(default_command, FileTest.IS_EXECUTABLE))
                            return;
                        try
                        {
                            string[] argv = { "pkexec", "update-alternatives", "--set", group, default_command };
                            Pid child_pid;
                            Process.spawn_async(null, argv, null,
                                SpawnFlags.SEARCH_PATH | SpawnFlags.DO_NOT_REAP_CHILD,
                                null, out child_pid);
                            ChildWatch.add(child_pid, (pid, status) => {
                                Process.close_pid(pid);
                            });
                        }
                        catch (SpawnError err)
                        {
                            warning (err.message);
                        }
                    });
                    box.append (button);
                    break;
                default:
                    if (more_list[a] != null)
                    {
                        var hbox = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
                        master_vbox.append (hbox);
                        var label = new Label(more_list[a]);
                        hbox.append (label);
                        var entry = new Entry();
                        string default_text = dbus_backend.Get(item, more_list[a]);
                        entry.text = default_text;
                        entry.changed.connect(() => {
                            if (entry.text != null)
                            {
                                dbus_backend.Set(item, more_list[a], entry.text);
                            }
                        });
                        entry.show();
                        hbox.append (entry);
                    }
                    break;
            }
        }
        window.present();
    }

    public class ComboItemObject: GLib.Object
    {
        public string name { get; set;}
        public string exec { get; set;}
        public Gtk.Image icon { get; set;}
        public string icon_name { get; set;}
        public string desktop_path { get; set;}
        public string install_package { get; set;}

        public string[] tmp_item_array;

        public ComboItemObject(string item)
        {
            this.tmp_item_array = item.split_set(",",0);
            this.name = tmp_item_array[0];
            this.exec = tmp_item_array[1];
            this.icon = create_image(tmp_item_array[2]);
            this.icon_name = tmp_item_array[2];
            this.desktop_path = tmp_item_array[3];
            this.install_package = tmp_item_array[4];
        }

        public Gtk.Image create_image (string item_image_string)
        {
            Gtk.Image image = new Gtk.Image ();

            if (item_image_string[0:1] == "/")
            {
                /* Absolute path for the icon, load it directly */
                image.set_from_file(item_image_string);
            }
            else
            {
                /* Name icon, load it by the name */
                image.set_from_icon_name (item_image_string);
            }

            return image;

        }
    }

    Gtk.ComboBox init_combobox (    Gtk.Builder builder,
                                    DbusBackend dbus_backend,
                                    KeyFile kf,
                                    string combobox_name,
                                    string combobox_list_name,
                                    string by_default,
                                    string? generic_item)
    {

        var return_combobox = builder.get_object (combobox_name) as Gtk.ComboBox;

        Gtk.ListStore list_store = new Gtk.ListStore (4, typeof(string), typeof (string), typeof (int) , typeof(string));
	    Gtk.TreeIter iter;
        int default_index = -1;
        bool default_set = false;

        string[] combobox_list;

        if (generic_item != null)
        {
            combobox_list = get_mime_list(kf, generic_item, "installed");
        }
        else
        {
            combobox_list = get_mime_list(kf, combobox_list_name, "installed");
        }

        /* First row, empty for not selected and for unselect */
        list_store.append (out iter);
        list_store.set (iter, 0, "window-close" , 1, _("Disable"), 2, 0, 3,"");

        for (int a = 0 ; a < combobox_list.length ; a++)
        {
                string item_list = combobox_list[a];
                ComboItemObject combo_item = new ComboItemObject (item_list);

                list_store.append (out iter);

                list_store.set (iter, 0, combo_item.icon_name , 1, combo_item.name, 2, a, 3, combo_item.exec );
                if (combo_item.exec == by_default)
                {
                    /* move +1, because 1st row is Disable */
                    default_index = a + 1;
                    default_set = true;
                }
        }

        message ("Default = %s", by_default);

        if (by_default == "" )
        {
            default_index = 0;
            default_set = true;
        }
        if (default_set == false)
        {
            Value val;
            int last_position;
            list_store.append (out iter);
            list_store.get_value (iter, 2, out val);
            last_position = val.get_int() + 1;
            list_store.set (iter, 0, "" , 1, by_default, 2, last_position, 3,by_default);
        }

        return_combobox.set_model (list_store);

        Gtk.CellRendererPixbuf renderer_image = new Gtk.CellRendererPixbuf ();
        return_combobox.pack_start (renderer_image, false);
        return_combobox.add_attribute (renderer_image, "icon-name", 0);

        Gtk.CellRendererText renderer = new Gtk.CellRendererText ();
        return_combobox.pack_start (renderer, false);
        return_combobox.add_attribute (renderer, "text", 1);

/*
            Debug Exec name

            Gtk.CellRendererText renderer_exec = new Gtk.CellRendererText ();
            return_combobox.pack_start (renderer_exec, true);
            return_combobox.add_attribute (renderer_exec, "text", 3);
*/


        return_combobox.active = 0;

        /* Set default */
        if (default_index == -1)
        {
            message ("Iter == -1");
            switch (by_default)
            {
                case (null):
                    return_combobox.set_active(0);
                    break;
                case (""):
                    return_combobox.set_active(0);
                    break;
                case (" "):
                    return_combobox.set_active(0);
                    break;
                default:
                    return_combobox.set_active_iter(iter);
                    break;
            }
        }
        else
        {
            message ("Iter == %d", default_index);
            return_combobox.set_active(default_index);
        }

        return_combobox.changed.connect (() => {
            Value val1;
            Value val2;

            return_combobox.get_active_iter (out iter);
            list_store.get_value (iter, 2, out val1);
            list_store.get_value (iter, 3, out val2);

            message ("Selection: %d, %s\n", (int) val1, (string) val2);

            if (val2.get_string() != null)
            {
                dbus_backend.Set(combobox_list_name, "command", val2.get_string());
                message ("Setting %s: %d, %s\n", combobox_list_name, (int) val1, (string) val2);
            }

            /* Browser links are applied centrally by SessionSet, without
             * also taking over every image/PDF type in the browser launcher. */
            if (combobox_list_name != "webbrowser")
                create_mime_associate_window(dbus_backend, kf, combobox_list_name, val2.get_string());

        });

        /* Disconnect scroll event, to avoid changing item when we are scrolling the windows */
        var scroll_controller = new Gtk.EventControllerScroll (Gtk.EventControllerScrollFlags.VERTICAL);
        scroll_controller.scroll.connect (() => {
            return true;
        });
        return_combobox.add_controller (scroll_controller);

        return return_combobox;
    }

    void create_mime_associate_window(DbusBackend dbus_backend, KeyFile kf, string combobox_list_name, string command)
    {
        var window_mime = new Window();
        window_mime.set_default_size (400, 200);
        window_mime.icon_name = "preferences-desktop";

        // The ScrolledWindow:
		Gtk.ScrolledWindow scrolled = new Gtk.ScrolledWindow ();
        scrolled.set_policy (PolicyType.NEVER, PolicyType.AUTOMATIC);
		window_mime.set_child (scrolled);

        var mime_view_port = new Gtk.Viewport(null, null);
        mime_view_port.set_size_request (200, 200);
        scrolled.set_child (mime_view_port);

        var mime_vbox = new Gtk.Box (Gtk.Orientation.VERTICAL, 0);
        mime_view_port.set_child (mime_vbox);

        var info_label = new Label(_("Do you want to associate the following Mimetype ?\n"));
        mime_vbox.append (info_label);

        string[] mime_combobox_list;
        mime_combobox_list = get_mime_list(kf, combobox_list_name, "installed");
        string default_path = "";

        for (int b = 0 ; b < mime_combobox_list.length ; b++)
        {
            string item_list = mime_combobox_list[b];
            ComboItemObject combo_item = new ComboItemObject (item_list);
            message("combo_item.desktop_path: %s", combo_item.desktop_path);
            message("combobox_list_name: %s", combobox_list_name);
            message("combo_item.exec: %s", combo_item.exec);

            if (command == combo_item.exec)
            {
                default_path = combo_item.desktop_path;
            }
        }

        message("Look at default_path: %s", default_path);

        if (default_path != "")
        {
            KeyFile kf_mime = new KeyFile();
            string[] mime_list;
            try
            {  
                load_key_file_bounded(kf_mime, default_path);
            }
            catch (KeyFileError err)
            {
                warning (err.message);
            }
            catch (FileError err)
            {
                warning (err.message);
            }

            try
            {
                mime_list = kf_mime.get_string_list("Desktop Entry", "MimeType");
                if (mime_list.length >= 1)
                {
                    string base_name = Path.get_basename(default_path);
                    var hbox_buttons = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
                    var ok_button = new Gtk.Button.with_label("OK");
                    var cancel_button = new Gtk.Button.with_label(_("Cancel"));
                    mime_vbox.append (hbox_buttons);
                    hbox_buttons.append (ok_button);
                    hbox_buttons.append (cancel_button);
                    message("Enter mime try");
                    for (int i = 0 ; i < mime_list.length ; i++)
                    {
                        string message_mime = mime_list[i] + "\n";
                        message("Look at message_mime: %s", message_mime);
                        var label_mime = new Label(message_mime);
                        mime_vbox.append (label_mime);
                    }

                    ok_button.clicked.connect(() => {
                        save_mime_type(mime_list, base_name);
                        window_mime.destroy();
                    });

                    cancel_button.clicked.connect(() => {
                        window_mime.destroy();
                    });

                    window_mime.present();
                }
                else
                {
                    window_mime.destroy();
                }
            }
            catch (KeyFileError err)
            {
                warning (err.message);
            }
        }
    }

    void save_mime_type(string[] mime_list, string base_name)
    {
        try
        {
            set_mime_defaults(mime_list, base_name);
        }
        catch (GLib.Error err)
        {
            warning (err.message);
        }
    }

    string return_combobox_text(Gtk.ComboBox combo)
    {
        Gtk.TreeIter iter;
        Gtk.ListStore model;
        GLib.Value value1;

        combo.get_active_iter (out iter);
        model = (Gtk.ListStore) combo.get_model ();
        model.get_value (iter, 0, out value1);

        message (" Return value for %s", (string) value1);

        return (string) value1;
    }


    string[] get_mime_list (KeyFile kf, string key1, string mode)
    {
        string keys = key1 + "/" + mode;
        string[] return_value = {};
        try
        {
            return_value = kf.get_string_list("Mime", keys);
        }
        catch (GLib.KeyFileError err)
        {
            warning (err.message);
        }
        return return_value;
    }

    public Gtk.ComboBox ui_combobox_init (  Gtk.Builder builder,
                                            string combobox_name,
                                            string[] combobox_list, 
                                            string? entry_name,
                                            string by_default)
    {
        Gtk.ListStore list_store = new Gtk.ListStore (2, typeof (string), typeof (int));
	    Gtk.TreeIter iter;
        int default_index = -1;

        for (int a = 0 ; a < combobox_list.length ; a++)
        {
                list_store.append (out iter);
                list_store.set (iter, 0, combobox_list[a], 1, a);
                if (combobox_list[a] == by_default)
                {
                    default_index = a;
                }
        }

        list_store.append (out iter);
        list_store.set (iter, 0, "Other", 1, 99);

        message ("Default = %s", by_default);

        var return_combobox = builder.get_object (combobox_name) as Gtk.ComboBox;
        return_combobox.set_model (list_store);

        Gtk.CellRendererText renderer = new Gtk.CellRendererText ();
        return_combobox.pack_start (renderer, true);
        return_combobox.add_attribute (renderer, "text", 0);
        return_combobox.active = 0;

        /* Set default */
        if (default_index == -1)
        {
            switch (by_default)
            {
                case (null):
                    return_combobox.set_active(0);
                    break;
                case (""):
                    return_combobox.set_active(0);
                    break;
                case (" "):
                    return_combobox.set_active(0);
                    break;
                default:
                    return_combobox.set_active_iter(iter);
                    if (entry_name != null)
                    {
                        var entry_default = builder.get_object (entry_name) as Entry;
                        entry_default.text = by_default;
                        entry_default.show();
                    }
                    break;
            }
        }
        else
        {
            message ("Iter == %d", default_index);
            return_combobox.set_active(default_index);
            if (entry_name != null)
            {
                var entry_default = builder.get_object (entry_name) as Entry;
                entry_default.hide();

            }
        }

        return_combobox.changed.connect (() => {
            Value val1;
            Value val2;

            return_combobox.get_active_iter (out iter);
            list_store.get_value (iter, 0, out val1);
            list_store.get_value (iter, 1, out val2);

            message ("Selection: %s, %d\n", (string) val1, (int) val2);

            if (entry_name != null)
            {
                var entry = builder.get_object (entry_name) as Entry;

                if (val2 == 99)
                {
                    entry.show();
                }
                else
                {
                    entry.hide();
                }
            }
        });

        return return_combobox;
    }
}
