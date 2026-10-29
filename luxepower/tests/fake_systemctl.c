/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main (int argc, char **argv)
{
  const char *system_dir = getenv ("LUXEPOWER_TEST_SYSTEM_DIR");
  const char *runtime_dir = getenv ("LUXEPOWER_TEST_RUNTIME_DIR");
  const char *counter_path = getenv ("LUXEPOWER_TEST_COUNTER");
  const char *fail_calls = getenv ("LUXEPOWER_TEST_FAIL_CALLS");
  int counter = 0;
  int runtime = argc == 4 && strcmp (argv[2], "--runtime") == 0;
  const char *verb = argc > 1 ? argv[1] : "";
  const char *target = runtime ? argv[3] : (argc > 2 ? argv[2] : "");

  if (!system_dir || !runtime_dir || !counter_path ||
      (argc != 3 && argc != 4) ||
      (strcmp (verb, "mask") && strcmp (verb, "unmask")))
    return 2;
  FILE *counter_file = fopen (counter_path, "r");
  if (counter_file) { fscanf (counter_file, "%d", &counter); fclose (counter_file); }
  counter++;
  counter_file = fopen (counter_path, "w");
  if (!counter_file) return 2;
  fprintf (counter_file, "%d", counter);
  fclose (counter_file);
  if (fail_calls)
    {
      const char *cursor = fail_calls;
      while (*cursor)
        {
          char *end = NULL;
          long requested = strtol (cursor, &end, 10);
          if (end == cursor) break;
          if (requested == counter) return 1;
          cursor = *end == ',' ? end + 1 : end;
        }
    }

  const char *directories[] = { system_dir, runtime_dir };
  for (int i = 0; i < (runtime ? 2 : 2); i++)
    {
      if ((runtime && i == 0) ||
          (!runtime && !strcmp (verb, "mask") && i == 1)) continue;
      char path[4096];
      snprintf (path, sizeof path, "%s/%s", directories[i], target);
      if (!strcmp (verb, "unmask")) unlink (path);
      else if (symlink ("/dev/null", path) != 0 && access (path, F_OK) != 0) return 1;
      if (runtime) break;
    }
  return 0;
}
