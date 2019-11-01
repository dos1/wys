/*
 * Copyright (C) 2019 Purism SPC
 *
 * This file is part of libmachine-check.
 *
 * libmachine-check is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * libmachine-check is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libmachine-check.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Bob Ham <bob.ham@puri.sm>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "mchk-machine-check.h"
#include "config.h"

#include <glib/gstdio.h>
#include <gio/gunixinputstream.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>


/**
 * mchk_read_machine:
 * @error: (allow-none): return location for an error, or %NULL
 *
 * Read the machine name from /proc/device-tree/machine.  If the
 * machine name could not be read, %NULL is returned and @error is
 * set.
 *
 * Returns: (nullable) (transfer full): The machine name or %NULL on
 * error.
 */
gchar *
mchk_read_machine (GError **error_out)
{
  static const gchar *MODEL_FILENAME = "/proc/device-tree/model";
  gchar *machine;
  GError *error = NULL;

  g_return_val_if_fail (error_out == NULL || *error_out == NULL, NULL);

  g_file_get_contents (MODEL_FILENAME,
                       &machine,
                       NULL,
                       &error);
  if (error)
    {
      g_warning ("Error reading machine name from `%s': %s",
                 MODEL_FILENAME, error->message);
      g_propagate_error (error_out, error);
    }

  return machine;
}


static gboolean
check_list_lines (const gchar  *filename,
                  int           fd,
                  const gchar  *machine,
                  gboolean     *present,
                  GError      **error_out)
{
  GInputStream *unix_stream;
  g_autoptr(GDataInputStream) data_stream = NULL;
  GError *error = NULL;

  g_debug ("Reading list file `%s'", filename);

  unix_stream = g_unix_input_stream_new (fd, TRUE);
  g_assert (unix_stream != NULL);

  data_stream = g_data_input_stream_new (unix_stream);
  g_assert (data_stream != NULL);
  g_object_unref (unix_stream);

  for (;;)
    {
      g_autofree gchar *line =
        g_data_input_stream_read_line_utf8
        (data_stream, NULL, NULL, &error);

      if (error)
        {
          g_warning ("Error reading from check"
                     " list file `%s': %s",
                     filename, error->message);
          g_propagate_error (error_out, error);
          return FALSE;
        }

      if (line)
        {
          g_strstrip (line);

          // Skip comments and empty lines
          if (line[0] == '#' || line[0] == '\0')
            {
              continue;
            }

          // Check for the machine name
          if (strcmp (line, machine) == 0)
            {
              *present = TRUE;
              return TRUE;
            }
        }
      else // EOF
        {
          *present = FALSE;
          return TRUE;
        }
    }
}


static gboolean
check_list (const gchar  *dirname,
            const gchar  *list,
            gboolean     *list_exists,
            const gchar  *machine,
            gboolean     *present,
            GError      **error)
{
  g_autofree gchar *filename = NULL;
  int fd;

  filename = g_build_filename (dirname, list, NULL);

  fd = g_open (filename, O_RDONLY, 0);
  if (fd == -1)
    {
      if (errno == ENOENT) // The file doesn't exist
        {
          if (list_exists)
            {
              *list_exists = FALSE;
            }
          *present = FALSE;
          return TRUE;
        }

      g_warning ("Error opening check"
                 " list file `%s': %s",
                 filename, g_strerror (errno));
      return FALSE;
    }

  if (list_exists)
    {
      *list_exists = TRUE;
    }

  return check_list_lines (filename,
                           fd,
                           machine,
                           present,
                           error);
}


static gboolean
check_dir_machine (const gchar  *dir,
                   const gchar  *param,
                   const gchar  *machine,
                   gboolean     *done,
                   gboolean     *passed,
                   GError      **error)
{
  g_autofree gchar *check_dirname = NULL;
  gboolean list_exists, present, ok;

  check_dirname = g_build_filename (dir, "machine-check",
                                    param, NULL);

  g_debug ("Trying machine-check directory `%s'",
           check_dirname);

  // Check the blacklist
  ok = check_list (check_dirname,
                   "blacklist",
                   NULL,
                   machine,
                   &present,
                   error);
  if (!ok)
    {
      return FALSE;
    }

  if (present)
    {
      if (passed)
        {
          *passed = FALSE;
        }
      *done = TRUE;
      return TRUE;
    }

  // Check the whitelist
  ok = check_list (check_dirname,
                   "whitelist",
                   &list_exists,
                   machine,
                   &present,
                   error);
  if (!ok)
    {
      return FALSE;
    }

  if (list_exists)
    {
      *done = TRUE;
      if (passed)
        {
          *passed = present;
        }
    }

  return TRUE;
}


/**
 * mchk_check_machine:
 * @param: the name of the machine-check sub-directory whose blacklist
 * and whitelist should be used
 * @machine: (allow-none): the machine name to check, or %NULL
 * @passed: (out) (allow-none): return location for the check result,
 * or %NULL
 * @error: (allow-none): return location for an error, or %NULL
 *
 * Check whether the machine name is not present in a blacklist and/or
 * present in a whitelist within a machine-check sub-directory
 * named @param.  If @machine is %NULL then mchk_read_machine() will
 * be used to get the machine name.  If an error is encountered,
 * @error will be set and %FALSE will be returned.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
mchk_check_machine (const gchar  *param,
                    const gchar  *machine,
                    gboolean     *passed,
                    GError      **error)
{
  g_autofree gchar *mach = NULL;
  const gchar * const *dirs, * const *dir;
  gboolean done = FALSE, ok;

  g_return_val_if_fail (param != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  // Get the machine name
  if (machine)
    {
      mach = g_strdup (machine);
    }
  else
    {
      mach = mchk_read_machine (error);
      if (!mach)
        {
          return FALSE;
        }
    }

  // Iterate over possible whitelist/blacklist locations
#define try_dir(d)                                      \
  ok = check_dir_machine (d, param, mach,               \
                          &done, passed, error);        \
  if (!ok || done)                                      \
    {                                                   \
      return ok;                                        \
    }


  try_dir (g_get_user_config_dir ());

  dirs = g_get_system_config_dirs ();
  for (dir = dirs; *dir; ++dir)
    {
      try_dir (*dir);
    }

  try_dir (SYSCONFDIR);
  try_dir (DATADIR);

  dirs = g_get_system_data_dirs ();
  for (dir = dirs; *dir; ++dir)
    {
      try_dir (*dir);
    }

#undef try_dir


  if (*passed)
    {
      *passed = TRUE;
    }
  return TRUE;
}
