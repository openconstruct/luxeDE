/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "cfg.h"
#include "const.h"
#include "image.h"
#include "log.h"
#include "util.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "check failed at line " << __LINE__ << ": " #condition "\n"; \
        return 1; \
    } \
} while (false)

int main()
{
    std::string first(32, '0');
    std::string second(32, '0');
    CHECK(Util::secure_random_hex(first));
    CHECK(Util::secure_random_hex(second));
    const std::string hex = "0123456789abcdef";
    CHECK(first.find_first_not_of(hex) == std::string::npos);
    CHECK(second.find_first_not_of(hex) == std::string::npos);
    CHECK(first != second);

    std::vector<std::string> values;
    Cfg::split(values, "luxe,openbox", ',', false);
    CHECK(values.size() == 2);
    CHECK(values[0] == "luxe");
    CHECK(values[1] == "openbox");
    Cfg::split(values, "", ',', false);
    CHECK(values.empty());

    CHECK(Cfg::absolutepos("50%", 1000, 200) == 400);
    {
        bool ok = false;
        CHECK(Cfg::string2int("2147483647", &ok) == 2147483647 && ok);
        CHECK(Cfg::string2int("2147483648", &ok) == 0 && !ok);
        CHECK(Cfg::string2int("12junk", &ok) == 0 && !ok);
        CHECK(Cfg::string2int(nullptr, &ok) == 0 && !ok);
        CHECK(Cfg::absolutepos("101%", 1000, 200) == 0);
        CHECK(Cfg::absolutepos("50x", 1000, 200) == 0);
        CHECK(Cfg::validThemeName("luxe"));
        CHECK(!Cfg::validThemeName("../etc"));
        CHECK(!Cfg::validThemeName("a/b"));
        Cfg parser_config;
        CHECK(!parser_config.parseOption(std::string("current_theme") + '\0' + "bad"));
        CHECK(parser_config.parseOption("console_cmd"));
        CHECK(parser_config.getOption("console_cmd").empty());
        CHECK(!parser_config.parseOption("current_theme"));
    }

    Cfg empty_config;
    const auto empty_session = empty_config.nextSession();
    CHECK(empty_session.name.empty() && empty_session.exec.empty());
    CHECK(empty_session.id == "luxe" && empty_session.desktop == "X-Luxe");
    CHECK(empty_config.findValidRandomTheme(",").empty());

    char directory_template[] = "/tmp/luxedm-sessions-XXXXXX";
    const char *directory = mkdtemp(directory_template);
    CHECK(directory != nullptr);
    const std::filesystem::path session_dir(directory);
    {
        std::ofstream(session_dir / "openbox.desktop")
            << "[Desktop Entry]\nName=Openbox\nExec=openbox-session\nDesktopNames=Openbox;\n";
        std::ofstream(session_dir / "luxe.desktop")
            << "[Desktop Entry]\nName=Luxe\nExec=start-luxe\nDesktopNames=X-Luxe;\n";
        std::ofstream config(session_dir / "test.conf");
        config << "sessiondir " << session_dir.string() << "\n"
               << "default_session Luxe\n";
    }
    Cfg config;
    CHECK(config.readConf((session_dir / "test.conf").string()));
    const auto selected = config.nextSession();
    CHECK(selected.name == "Luxe");
    CHECK(selected.exec == "start-luxe");
    CHECK(selected.id == "luxe");
    CHECK(selected.desktop == "X-Luxe");
    const SessionEnvironment luxe_env = sessionEnvironment(selected);
    CHECK(luxe_env.session_desktop == "X-Luxe");
    CHECK(luxe_env.current_desktop == "X-Luxe");
    CHECK(luxe_env.desktop_session == "luxe");
    {
        std::ofstream(session_dir / "test-openbox.conf")
            << "sessiondir " << session_dir.string() << "\n"
            << "default_session Openbox\n";
    }
    Cfg openbox_config;
    CHECK(openbox_config.readConf((session_dir / "test-openbox.conf").string()));
    const auto openbox = openbox_config.nextSession();
    CHECK(openbox.name == "Openbox" && openbox.id == "openbox");
    CHECK(openbox.desktop == "Openbox");
    const SessionEnvironment openbox_env = sessionEnvironment(openbox);
    CHECK(openbox_env.session_desktop == "Openbox");
    CHECK(openbox_env.current_desktop == "Openbox");
    CHECK(openbox_env.desktop_session == "openbox");
    const auto legacy_openbox = legacySessionChoice("openbox-session");
    const auto legacy_env = sessionEnvironment(legacy_openbox);
    CHECK(legacy_openbox.id == "openbox-session");
    CHECK(legacy_env.current_desktop == "Openbox");
    CHECK(legacy_env.desktop_session == "openbox-session");
    std::filesystem::remove_all(session_dir);

    const std::array<unsigned char, 12> rgb = {
        0, 0, 0, 64, 64, 64, 128, 128, 128, 255, 255, 255};
    const std::array<unsigned char, 4> alpha = {0, 64, 128, 255};
    Image interpolated(2, 2, rgb.data(), alpha.data());
    unsigned char pixel[3] = {0, 0, 0};
    unsigned char opacity = 0;
    interpolated.getPixel(0.5, 0.5, pixel, &opacity);
    CHECK(opacity == 112);
    CHECK(pixel[0] == 112 && pixel[1] == 112 && pixel[2] == 112);
    {
        const std::array<unsigned char, 6> edge_rgb = {
            0, 0, 0, 255, 255, 255};
        Image edge(2, 1, edge_rgb.data(), nullptr);
        edge.getPixel(1.5, 0.0, pixel);
        CHECK(pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255);
    }

    interpolated.Crop(1, 0, 1, 2);
    CHECK(interpolated.Width() == 1 && interpolated.Height() == 2);
    CHECK(interpolated.getPNGAlpha() != nullptr);
    CHECK(interpolated.getPNGAlpha()[0] == 64);
    CHECK(interpolated.getPNGAlpha()[1] == 255);
    interpolated.Crop(-1, 0, 1, 1);
    CHECK(interpolated.Width() == 1 && interpolated.Height() == 2);

    std::array<unsigned char, 48> grid = {};
    for (size_t i = 0; i < 16; ++i)
        grid[3 * i] = grid[3 * i + 1] = grid[3 * i + 2]
            = static_cast<unsigned char>(i);
    Image reduced(4, 4, grid.data(), nullptr);
    reduced.Reduce(1);
    CHECK(reduced.Width() == 2 && reduced.Height() == 2);
    CHECK(reduced.getRGBData()[0] == 2);
    reduced.Reduce(8);
    CHECK(reduced.Width() == 2 && reduced.Height() == 2);

    Image themed;
    CHECK(themed.Read(LUXEDM_TEST_THEME));
    CHECK(themed.Width() > 0 && themed.Height() > 0);
    CHECK(themed.getPNGAlpha() != nullptr);
    const int theme_width = themed.Width();
    const int theme_height = themed.Height();

    const std::filesystem::path broken_jpeg =
        std::filesystem::temp_directory_path() / "luxedm-broken-test.jpg";
    {
        std::ofstream output(broken_jpeg, std::ios::binary);
        const unsigned char contents[] = {0xff, 0xd8, 0xff, 0xd9};
        output.write(reinterpret_cast<const char *>(contents), sizeof(contents));
    }
    CHECK(!themed.Read(broken_jpeg.c_str()));
    CHECK(themed.Width() == theme_width && themed.Height() == theme_height);
    std::filesystem::remove(broken_jpeg);

    CHECK(Util::splitCommand("").empty());
    CHECK(Util::splitCommand("   \t  ").empty());
    {
        const auto simple = Util::splitCommand("/sbin/shutdown -h now");
        CHECK(simple.size() == 3);
        CHECK(simple[0] == "/sbin/shutdown" && simple[1] == "-h"
            && simple[2] == "now");
    }
    {
        const auto quoted = Util::splitCommand(
            "/usr/bin/xterm -T \"Console login\" -e /bin/login");
        CHECK(quoted.size() == 5);
        CHECK(quoted[2] == "Console login");
        CHECK(quoted[4] == "/bin/login");
    }
    {
        const auto single = Util::splitCommand("cmd 'a b' c");
        CHECK(single.size() == 3);
        CHECK(single[1] == "a b");
    }
    {
        const auto escaped = Util::splitCommand("cmd a\\ b c");
        CHECK(escaped.size() == 3);
        CHECK(escaped[1] == "a b");
        CHECK(escaped[2] == "c");
    }
    CHECK(Util::splitCommand("cmd 'unterminated").empty());
    CHECK(Util::splitCommand("cmd \"unterminated").empty());
    CHECK(Util::splitCommand("cmd trailing\\").empty());
    {
        setenv("LUXEDM_CORE_TEST_VAR", "core-value", 1);
        unsetenv("LUXEDM_CORE_TEST_UNSET");
        std::vector<std::string> args = {"$LUXEDM_CORE_TEST_VAR",
            "${LUXEDM_CORE_TEST_VAR}", "$LUXEDM_CORE_TEST_UNSET",
            "$$", "prefix-$LUXEDM_CORE_TEST_VAR-suffix"};
        Util::expandEnvVars(args);
        CHECK(args[0] == "core-value");
        CHECK(args[1] == "core-value");
        CHECK(args[2].empty());
        CHECK(args[3] == "$$");
        CHECK(args[4] == "prefix-core-value-suffix");
        unsetenv("LUXEDM_CORE_TEST_VAR");
    }
    {
        /* Expansion must not re-split: a value with spaces stays one arg. */
        setenv("LUXEDM_CORE_TEST_SPACED", "a b", 1);
        std::vector<std::string> args = {"$LUXEDM_CORE_TEST_SPACED"};
        Util::expandEnvVars(args);
        CHECK(args.size() == 1 && args[0] == "a b");
        unsetenv("LUXEDM_CORE_TEST_SPACED");
    }
    {
        /* %user substitution is literal: metacharacters stay one argument. */
        std::vector<std::string> args =
            Util::splitCommand("/usr/bin/sessreg -a -l \"$DISPLAY\" %user");
        setenv("DISPLAY", ":0.0", 1);
        Util::expandEnvVars(args);
        Util::replacePlaceholder(args, "%user", "a; rm -rf /");
        CHECK(args.size() == 5);
        CHECK(args[3] == ":0.0");
        CHECK(args[4] == "a; rm -rf /");
    }
    CHECK(Util::spawnAndWait({"/bin/true"}));
    CHECK(!Util::spawnAndWait({"/bin/false"}));
    CHECK(!Util::spawnAndWait({"/nonexistent-luxedm-command"}));
    CHECK(!Util::spawnAndWait({}));
    CHECK(!Util::add_mcookie("bad\ncookie", ":0", "/usr/bin/xauth", "/tmp/luxedm-test-auth"));
    CHECK(!Util::add_mcookie("0123456789abcdef", ":0\nadd :1 . attacker", "/usr/bin/xauth", "/tmp/luxedm-test-auth"));
    CHECK(Util::runCommand(""));
    CHECK(Util::runCommand("   "));
    CHECK(!Util::runCommand("cmd 'unterminated"));
    CHECK(Util::runCommand("/bin/true"));
    CHECK(!Util::runCommand("/bin/false"));
    /* No shell: ';' is a literal argument, so the second command never runs
     * and the bogus program name fails closed. */
    CHECK(!Util::runCommand("false; /bin/true"));
    {
        const std::filesystem::path marker =
            std::filesystem::temp_directory_path() / "luxedm-noshell-marker";
        std::filesystem::remove(marker);
        CHECK(Util::runCommand(
            std::string("/bin/echo hi; touch ") + marker.string()));
        CHECK(!std::filesystem::exists(marker));
    }

    /* Root screenshot path removed: the option parses but stays empty. */
    {
        Cfg defaults;
        CHECK(defaults.getOption("screenshot_cmd").empty());
    }
    /* Console command disabled by default (P1 #6): an authenticated
     * Console action is a silent no-op unless the admin configures it. */
    {
        Cfg defaults;
        CHECK(defaults.getOption("console_cmd").empty());
    }
    /* Image bounds: 10000^2 (~400 MB) rejected, 4K-side cap with byte budget. */
    CHECK(MAX_DIMENSION >= 4096 && MAX_DIMENSION <= 6000);
    CHECK(MAX_IMAGE_BYTES <= 128 * 1024 * 1024);
    {
        Image oversized;
        oversized.Plain(10000, 10000, "ff0000");
        CHECK(oversized.Width() == 0 && oversized.Height() == 0);
        Image small;
        small.Plain(8, 8, "ff0000");
        CHECK(small.Width() == 8 && small.Height() == 8);
        small.Resize(10000, 10000);
        CHECK(small.Width() == 8 && small.Height() == 8);
        small.Tile(10000, 10000);
        CHECK(small.Width() == 8 && small.Height() == 8);
        small.Center(10000, 10000, "ff0000");
        CHECK(small.Width() == 8 && small.Height() == 8);
        Image missing;
        CHECK(!missing.Read("/nonexistent-luxedm-image.png"));
    }
    {
        const std::filesystem::path log_path =
            std::filesystem::temp_directory_path() /
            ("luxedm-log-test-" + std::to_string(getpid()));
        const std::filesystem::path link_path = log_path.string() + ".link";
        std::ofstream(log_path) << "log";
        chmod(log_path.c_str(), 0600);
        std::filesystem::remove(link_path);
        std::filesystem::create_symlink(log_path, link_path);
        LogUnit logger;
        CHECK(!logger.openLog(nullptr));
        CHECK(!logger.openLog(link_path.c_str()));
        CHECK(logger.openLog(log_path.c_str()));
        logger.closeLog();
        std::filesystem::remove(link_path);
        std::filesystem::remove(log_path);
    }
    return 0;
}
