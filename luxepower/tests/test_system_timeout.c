/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/system.h"

int main (void)
{
  HelperResult *result = system_spawn_capture (
      (const char *const[]) { "/bin/sh", "-c", "sleep 10 & wait", NULL }, NULL);
  g_assert_cmpint (result->code, ==, -1);
  g_assert_nonnull (strstr (result->err, "exceeded its deadline"));
  helper_result_free (result);
  return 0;
}
