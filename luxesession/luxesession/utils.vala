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

namespace Lxsession{
    [CCode (cname = "lxsession_key_file_load_bounded")]
    private bool load_key_file_bounded (KeyFile key_file, string path)
        throws GLib.Error;

public string session_global;

[CCode (cname = "luxe_profile_component_valid")]
public bool valid_profile_component (string value);

public struct AppType {
    public string name;
    public string[] command;
    public bool guard;
    public string application_type;
}

public KeyFile load_keyfile (string config_path) {

    KeyFile kf = new KeyFile();

    try {
        load_key_file_bounded(kf, config_path);
    } catch (KeyFileError err) {
        warning (err.message);
    } catch (FileError err) {
        warning (err.message);
    }

    return kf;
}

public string get_config_home_path (string conf_file)
{

    string user_config_dir = Path.build_filename(
                             Environment.get_user_config_dir (),
                             "luxesession",
                             session_global,
                             conf_file);

    return user_config_dir;

}


public string get_config_path (string conf_file) {

    string final_config_file;

    string user_config_dir = get_config_home_path(conf_file);

    if (FileUtils.test (user_config_dir, FileTest.EXISTS))
    {
        message ("User config used : %s", user_config_dir);
        final_config_file = user_config_dir;
    }
    else
    {
        string[] system_config_dirs = Environment.get_system_config_dirs ();
        string config_system_location = null;
        string path_system_config_file = null;

        foreach (string config in (system_config_dirs)) {
            config_system_location = Path.build_filename (config, "luxesession", session_global);
            message ("Config system location : %s", config_system_location);
            if (FileUtils.test (config_system_location, FileTest.EXISTS)) {
                path_system_config_file = Path.build_filename (config_system_location, conf_file);
                break;
            }
        }
      message ("System system path location : %s", path_system_config_file);
      final_config_file =  path_system_config_file;

     }
     message ("Final file used : %s", final_config_file);
     return final_config_file;

}

public class LxSignals : Object
{
    public signal void update_window_manager (string dbus_arg, string kf_categorie = "Session", string kf_key1 = "window_manager", string? kf_key2 = null);

    /* Xsettings */
    public signal void reload_settings_daemon();

    public signal void generic_set_signal (string categorie, string key1, string? key2, string type, string dbus_arg);

}

public bool detect_laptop()
{
    string test_laptop_detect = Environment.find_program_in_path("laptop-detect");
    if (test_laptop_detect != null)
    {
        int exit_status;
        string standard_output, standard_error;
        try
        {
            Process.spawn_command_line_sync ("laptop-detect", out standard_output,
                                                              out standard_error,
                                                              out exit_status);
            /* exit_status is wait-encoded: decode it instead of comparing raw. */
            if (Posix.WIFEXITED (exit_status) && Posix.WEXITSTATUS (exit_status) == 0)
            {
                message ("Laptop detect return true");
                return true;
            }
            else
            {
                if (Posix.WIFSIGNALED (exit_status))
                {
                    message ("Laptop detect killed by signal %d", Posix.WTERMSIG (exit_status));
                }
                else
                {
                    message ("Laptop detect return false");
                }
                return false;
            }
        }
        catch (SpawnError err)
        {
            warning (err.message);
            return false;
        }
    }
    else
    {
        message ("Laptop detect not find");
        /* TODO check upower, and /proc files like laptop-detect to find bateries */
        return false;
    }
}

public bool check_package_manager_running ()
{
    GLib.File dpkg, apt_archives, apt_lists, unattended_upgrades;
    bool return_value = false;

    dpkg = File.new_for_path("/var/lib/dpkg/lock");
    apt_archives = File.new_for_path("/var/cache/apt/archives/lock");
    apt_lists = File.new_for_path("/var/lib/apt/lists/lock");
    unattended_upgrades = File.new_for_path("/var/run/unattended-upgrades.lock");

    if (dpkg.query_exists ())
    {
        return_value = true;
    }

    if (apt_archives.query_exists ())
    {
        return_value = true;
    }

    if (apt_lists.query_exists ())
    {
        return_value = true;
    }

    if (unattended_upgrades.query_exists ())
    {
        return_value = true;
    }

    return return_value;
}

}
