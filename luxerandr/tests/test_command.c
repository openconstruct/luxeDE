/*
 *      test_command.c - behavioral tests for the xrandr command builder
 *
 *      Covers the six cases previously checked with grep scripts:
 *      normal/autostart, eDP-only, external-only, disabled-anchor,
 *      3-monitor chaining, and zero-mode.
 *
 *      Copyright (C) 2008 Hong Jen Yee(PCMan) <pcman.tw@gmail.com>
 *      Copyright (C) 2011 Julien Lavergne <julien.lavergne@gmail.com>
 *      Copyright (C) 2014 Andriy Grytsenko (LStranger) <andrej@rep.kiev.ua>
 *      Copyright (C) 2026 Jerry Howell
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>

#include "command.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, fmt, ...) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("  FAIL: " fmt "\n", ##__VA_ARGS__); \
    } \
} while (0)

static char **
make_mode_line(const char *mode, const char *r1, const char *r2)
{
    int n = 2 + (r1 != NULL) + (r2 != NULL);
    char **v = g_new0(char *, n);
    int i = 0;
    v[i++] = g_strdup(mode);
    if (r1)
        v[i++] = g_strdup(r1);
    if (r2)
        v[i++] = g_strdup(r2);
    v[i] = NULL;
    return v;
}

static Monitor *
make_monitor(const char *name, int try_mode, int try_rate, MonitorPlacement p)
{
    Monitor *m = g_new0(Monitor, 1);
    m->name = g_strdup(name);
    m->try_mode = (short)try_mode;
    m->try_rate = (short)try_rate;
    m->try_placement = p;
    m->placement = p;
    m->active_mode = (short)try_mode;
    m->active_rate = (short)try_rate;
    return m;
}

static void
reset_state(void)
{
    free_monitors();
}

/* 1. normal/autostart: LVDS fixed auto, external mode right-of LVDS.
 * The generated string must be direct xrandr arguments (no shell
 * wrapper) so it is valid both on the command line and in a Desktop
 * Entry Exec key. */
static void
test_normal(void)
{
    Monitor *lvds, *ext;
    GString *cmd;
    printf("case normal/autostart\n");
    reset_state();
    lvds = make_monitor("LVDS1", 0, 0, PLACEMENT_DEFAULT);
    ext = make_monitor("HDMI1", 1, 0, PLACEMENT_RIGHT);
    ext->mode_lines = g_slist_append(ext->mode_lines,
                                     make_mode_line("1920x1080", "60.00", "59.94"));
    monitors = g_slist_append(monitors, lvds);
    monitors = g_slist_append(monitors, ext);
    LVDS = lvds;

    cmd = get_command_xrandr_info();
    CHECK(cmd != NULL, "expected non-NULL command");
    if (cmd)
    {
        CHECK(strncmp(cmd->str, "xrandr", 6) == 0,
              "command must start with xrandr, got: %s", cmd->str);
        CHECK(strstr(cmd->str, "sh -c") == NULL,
              "command must not use a shell wrapper, got: %s", cmd->str);
        CHECK(strstr(cmd->str, "--output LVDS1 --auto") != NULL,
              "missing LVDS auto segment, got: %s", cmd->str);
        CHECK(strstr(cmd->str, "--output HDMI1 --mode 1920x1080") != NULL,
              "missing HDMI mode segment, got: %s", cmd->str);
        CHECK(strstr(cmd->str, "--right-of") != NULL &&
              strstr(cmd->str, "LVDS1") != NULL,
              "HDMI must anchor right-of LVDS, got: %s", cmd->str);
        g_string_free(cmd, TRUE);
    }
    reset_state();
}

/* 2. eDP-only: a lone modern laptop panel is the fixed output and
 * carries no placement flag. */
static void
test_edp_only(void)
{
    Monitor *edp;
    GString *cmd;
    printf("case eDP-only\n");
    reset_state();
    edp = make_monitor("eDP1", 0, 0, PLACEMENT_DEFAULT);
    monitors = g_slist_append(monitors, edp);
    LVDS = edp;

    cmd = get_command_xrandr_info();
    CHECK(cmd != NULL, "expected non-NULL command");
    if (cmd)
    {
        CHECK(strstr(cmd->str, "--output eDP1 --auto") != NULL,
              "missing eDP auto segment, got: %s", cmd->str);
        CHECK(strstr(cmd->str, "--right-of") == NULL &&
              strstr(cmd->str, "--same-as") == NULL &&
              strstr(cmd->str, "--left-of") == NULL &&
              strstr(cmd->str, "--above") == NULL &&
              strstr(cmd->str, "--below") == NULL,
              "single output must have no placement flag, got: %s", cmd->str);
        g_string_free(cmd, TRUE);
    }
    reset_state();
}

/* 3. external-only: disabled LVDS must not be used as the anchor;
 * the enabled external output becomes the fixed reference. */
static void
test_external_only(void)
{
    Monitor *lvds, *ext;
    GString *cmd;
    printf("case external-only\n");
    reset_state();
    lvds = make_monitor("eDP1", -1, 0, PLACEMENT_DEFAULT);
    ext = make_monitor("HDMI1", 0, 0, PLACEMENT_DEFAULT);
    monitors = g_slist_append(monitors, lvds);
    monitors = g_slist_append(monitors, ext);
    LVDS = lvds;

    cmd = get_command_xrandr_info();
    CHECK(cmd != NULL, "expected non-NULL command");
    if (cmd)
    {
        CHECK(strstr(cmd->str, "--output eDP1 --off") != NULL,
              "LVDS must be off, got: %s", cmd->str);
        CHECK(strstr(cmd->str, "--output HDMI1 --auto") != NULL,
              "external must be auto, got: %s", cmd->str);
        CHECK(strstr(cmd->str, "--right-of") == NULL &&
              strstr(cmd->str, "--same-as") == NULL &&
              strstr(cmd->str, "--left-of") == NULL &&
              strstr(cmd->str, "--above") == NULL &&
              strstr(cmd->str, "--below") == NULL,
              "fixed external output must have no anchor, got: %s", cmd->str);
        g_string_free(cmd, TRUE);
    }
    reset_state();
}

/* 4. disabled-anchor: the first list entry is off, so placement must
 * resolve to the first enabled output, never monitors->data. */
static void
test_disabled_anchor(void)
{
    Monitor *off, *lvds, *ext;
    GString *cmd;
    char *anchor_target;
    printf("case disabled-anchor\n");
    reset_state();
    off = make_monitor("VGA1", -1, 0, PLACEMENT_DEFAULT);
    lvds = make_monitor("eDP1", 0, 0, PLACEMENT_DEFAULT);
    ext = make_monitor("HDMI1", 1, 0, PLACEMENT_RIGHT);
    ext->mode_lines = g_slist_append(ext->mode_lines,
                                     make_mode_line("1920x1080", "60.00", NULL));
    monitors = g_slist_append(monitors, off);
    monitors = g_slist_append(monitors, lvds);
    monitors = g_slist_append(monitors, ext);
    LVDS = lvds;

    cmd = get_command_xrandr_info();
    CHECK(cmd != NULL, "expected non-NULL command");
    if (cmd)
    {
        CHECK(strstr(cmd->str, "--output VGA1 --off") != NULL,
              "first output must be off, got: %s", cmd->str);
        anchor_target = strstr(cmd->str, "--right-of");
        CHECK(anchor_target != NULL, "missing --right-of, got: %s", cmd->str);
        if (anchor_target)
        {
            CHECK(strstr(anchor_target, "eDP1") != NULL,
                  "anchor must be eDP1, got: %s", cmd->str);
            CHECK(strstr(anchor_target, "VGA1") == NULL,
                  "disabled anchor must never be used, got: %s", cmd->str);
        }
        g_string_free(cmd, TRUE);
    }
    reset_state();
}

/* 5. three-monitor: extended outputs chain via the nearest preceding
 * enabled output instead of stacking on one anchor. */
static void
test_three_monitor(void)
{
    Monitor *lvds, *m2, *m3;
    GString *cmd;
    char *second, *third;
    printf("case three-monitor\n");
    reset_state();
    lvds = make_monitor("LVDS1", 0, 0, PLACEMENT_DEFAULT);
    m2 = make_monitor("HDMI1", 1, 0, PLACEMENT_RIGHT);
    m2->mode_lines = g_slist_append(m2->mode_lines,
                                    make_mode_line("1920x1080", "60.00", NULL));
    m3 = make_monitor("DP1", 1, 0, PLACEMENT_RIGHT);
    m3->mode_lines = g_slist_append(m3->mode_lines,
                                    make_mode_line("1280x1024", "60.02", NULL));
    monitors = g_slist_append(monitors, lvds);
    monitors = g_slist_append(monitors, m2);
    monitors = g_slist_append(monitors, m3);
    LVDS = lvds;

    cmd = get_command_xrandr_info();
    CHECK(cmd != NULL, "expected non-NULL command");
    if (cmd)
    {
        second = strstr(cmd->str, "--output HDMI1");
        third = strstr(cmd->str, "--output DP1");
        CHECK(second != NULL && third != NULL,
              "missing outputs, got: %s", cmd->str);
        if (second)
        {
            char *flag = strstr(second, "--right-of");
            CHECK(flag != NULL && flag < third,
                  "HDMI must anchor right-of LVDS, got: %s", cmd->str);
            if (flag)
                CHECK(strstr(flag, "LVDS1") != NULL && (third == NULL || flag < third),
                      "HDMI anchor must be LVDS1, got: %s", cmd->str);
        }
        if (third)
        {
            char *flag = strstr(third, "--right-of");
            CHECK(flag != NULL, "DP must have --right-of, got: %s", cmd->str);
            if (flag)
                CHECK(strstr(flag, "HDMI1") != NULL,
                      "DP must chain to HDMI1, got: %s", cmd->str);
        }
        g_string_free(cmd, TRUE);
    }
    reset_state();
}

/* 6. zero-mode: an output with no usable mode_lines and a stale rate
 * must yield NULL (unavailable value) instead of reading past the
 * NULL terminator. */
static void
test_zero_mode(void)
{
    Monitor *m;
    GString *cmd;
    printf("case zero-mode\n");
    reset_state();
    m = make_monitor("HDMI1", 1, 1, PLACEMENT_DEFAULT);
    /* no mode_lines at all */
    monitors = g_slist_append(monitors, m);
    LVDS = NULL;

    cmd = get_command_xrandr_info();
    CHECK(cmd == NULL, "zero-mode output must produce NULL");
    if (cmd)
        g_string_free(cmd, TRUE);
    reset_state();
}

int
main(void)
{
    GString *token = g_string_new("xrandr");
    CHECK(!append_xrandr_token(token, "--off"), "leading dash rejected");
    CHECK(append_xrandr_token(token, "HDMI; dock"), "quoted connector accepted");
    CHECK(!append_xrandr_token(token, NULL), "NULL rejected");
    CHECK(append_xrandr_token(token, "DP-1.2"), "MST connector accepted");
    g_string_free(token, TRUE);

    reset_state();
    Monitor *panel=make_monitor("eDP-1",0,0,PLACEMENT_DEFAULT);
    Monitor *external=make_monitor("DP One",1,0,PLACEMENT_DEFAULT);
    external->mode_lines=g_slist_append(external->mode_lines,
        make_mode_line("Wide 1 \"custom\"", "60.00", NULL));
    panel->has_position=external->has_position=TRUE;
    panel->x=0;panel->y=0;external->x=2100;external->y=-220;
    monitors=g_slist_append(monitors,panel);monitors=g_slist_append(monitors,external);LVDS=panel;
    GString *cmd=get_command_xrandr_info();
    CHECK(cmd && strstr(cmd->str,"--pos 2100x-220"),"untouched irregular coordinates preserved");
    g_string_free(cmd,TRUE);
    external->try_placement=PLACEMENT_RIGHT;
    cmd=get_command_xrandr_info();
    CHECK(cmd && strstr(cmd->str,"--right-of eDP-1"),"edited placement uses a relative option (got: %s)",cmd?cmd->str:"NULL");
    g_string_free(cmd,TRUE);
    g_auto(GStrv) argv=NULL;gint argc=0;
    cmd=get_command_xrandr_info();
    CHECK(cmd && g_shell_parse_argv(cmd->str,&argc,&argv,NULL),"shell-free guard command parses");
    gboolean found_mode=FALSE,found_connector=FALSE;
    for(gint i=0;argv && argv[i];i++) {
        if(g_str_equal(argv[i],"Wide 1 \"custom\"")) found_mode=TRUE;
        if(g_str_equal(argv[i],"DP One")) found_connector=TRUE;
    }
    CHECK(found_mode && found_connector,"spaces and quotes remain single argv values");
    g_strfreev(g_steal_pointer(&argv));g_string_free(cmd,TRUE);
    cmd=get_desktop_exec_xrandr_info();
    CHECK(cmd && strstr(cmd->str,"\"DP One\"") && strstr(cmd->str,"\\\"custom\\\""),
          "Desktop Exec serializer quotes values independently");
    g_string_free(cmd,TRUE);
    reset_state();
    Monitor *disabled = make_monitor("DP-1", -1, 0, PLACEMENT_DEFAULT);
    monitors = g_slist_append(NULL, disabled);
    GString *invalid = get_command_xrandr_info();
    CHECK(invalid == NULL, "all-disabled layout rejected");
    disabled->try_mode = 0;
    disabled->try_placement = 99;
    invalid = get_command_xrandr_info();
    CHECK(invalid == NULL, "unknown placement rejected");
    reset_state();
    test_normal();
    test_edp_only();
    test_external_only();
    test_disabled_anchor();
    test_three_monitor();
    test_zero_mode();
    printf("checks: %d failures: %d\n", checks, failures);
    if (failures == 0)
        printf("ALL BEHAVIORAL CASES PASSED\n");
    return failures == 0 ? 0 : 1;
}
