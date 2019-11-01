/*
 * Copyright (C) 2019 Purism SPC
 *
 * This file is part of Wys.
 *
 * Wys is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wys is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wys.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Bob Ham <bob.ham@puri.sm>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "wys-modem.h"
#include "wys-audio.h"
#include "util.h"
#include "config.h"
#include "mchk-machine-check.h"

#include <libmm-glib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gunixinputstream.h>

#include <stdio.h>
#include <locale.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>


#define TTY_CHUNK_SIZE   320
#define SAMPLE_LEN       2

static GMainLoop *main_loop = NULL;

struct wys_data
{
  /** PulseAudio interface */
  WysAudio *audio;
  /** ID for the D-Bus watch */
  guint watch_id;
  /** ModemManager object proxy */
  MMManager *mm;
  /** Map of D-Bus object paths to WysModems */
  GHashTable *modems;
  /** How many modems have audio calls */
  guint audio_count;
};


static void
update_audio_count (struct wys_data *data,
                    gint             delta)
{
  const guint old_count = data->audio_count;

  g_assert (delta >= 0 || data->audio_count > 0);

  data->audio_count += delta;

  if (data->audio_count > 0 && old_count == 0)
    {
      g_debug ("Audio now present");
      wys_audio_ensure_loopback (data->audio);
    }
  else if (data->audio_count == 0 && old_count > 0)
    {
      g_debug ("Audio now absent");
      wys_audio_ensure_no_loopback (data->audio);
    }
}

static void
audio_present_cb (struct wys_data *data,
                  WysModem        *modem)
{
  update_audio_count (data, +1);
}


static void
audio_absent_cb (struct wys_data *data,
                 WysModem        *modem)
{
  update_audio_count (data, -1);
}


static void
add_modem (struct wys_data *data,
           GDBusObject     *object)
{
  const gchar *path;
  MMModemVoice *voice;
  WysModem *modem;

  path = g_dbus_object_get_object_path (object);
  if (g_hash_table_contains (data->modems, path))
    {
      g_warning ("New voice interface on existing"
                 " modem with path `%s'", path);
      return;
    }

  g_debug ("Adding new voice-capable modem `%s'", path);

  g_assert (MM_IS_OBJECT (object));
  voice = mm_object_get_modem_voice (MM_OBJECT (object));
  g_assert (voice != NULL);

  modem = wys_modem_new (voice);

  g_hash_table_insert (data->modems,
                       strdup (path),
                       modem);

  g_signal_connect_swapped (modem, "audio-present",
                            G_CALLBACK (audio_present_cb),
                            data);
  g_signal_connect_swapped (modem, "audio-absent",
                            G_CALLBACK (audio_absent_cb),
                            data);
}


static void
interface_added_cb (struct wys_data *data,
                    GDBusObject     *object,
                    GDBusInterface  *interface)
{
  GDBusInterfaceInfo *info;

  info = g_dbus_interface_get_info (interface);

  g_debug ("ModemManager interface `%s' found on object `%s'",
           info->name,
           g_dbus_object_get_object_path (object));

  if (g_strcmp0 (info->name, MM_DBUS_INTERFACE_MODEM_VOICE) == 0)
    {
      add_modem (data, object);
    }
}


static void
remove_modem_object (struct wys_data *data,
                     const gchar     *path,
                     GDBusObject     *object)
{
  g_hash_table_remove (data->modems, path);
}


static void
interface_removed_cb (struct wys_data *data,
                      GDBusObject     *object,
                      GDBusInterface  *interface)
{
  const gchar *path;
  GDBusInterfaceInfo *info;

  path = g_dbus_object_get_object_path (object);
  info = g_dbus_interface_get_info (interface);

  g_debug ("ModemManager interface `%s' removed on object `%s'",
           info->name, path);

  if (g_strcmp0 (info->name, MM_DBUS_INTERFACE_MODEM_VOICE) != 0)
    {
      remove_modem_object (data, path, object);
    }
}


static void
add_mm_object (struct wys_data *data, GDBusObject *object)
{
  GList *ifaces, *node;

  ifaces = g_dbus_object_get_interfaces (object);
  for (node = ifaces; node; node = node->next)
    {
      interface_added_cb (data, object,
                          G_DBUS_INTERFACE (node->data));
    }

  g_list_free_full (ifaces, g_object_unref);
}


static void
add_mm_objects (struct wys_data *data)
{
  GList *objects, *node;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (data->mm));
  for (node = objects; node; node = node->next)
    {
      add_mm_object (data, G_DBUS_OBJECT (node->data));
    }

  g_list_free_full (objects, g_object_unref);
}


void
object_added_cb (struct wys_data *data,
                 GDBusObject     *object)
{
  g_debug ("ModemManager object `%s' added",
           g_dbus_object_get_object_path (object));

  add_mm_object (data, object);
}


void
object_removed_cb (struct wys_data *data,
                   GDBusObject     *object)
{
  const gchar *path;

  path = g_dbus_object_get_object_path (object);
  g_debug ("ModemManager object `%s' removed", path);

  remove_modem_object (data, path, object);
}


static void
mm_manager_new_cb (GDBusConnection *connection,
                   GAsyncResult *res,
                   struct wys_data *data)
{
  GError *error = NULL;

  data->mm = mm_manager_new_finish (res, &error);
  if (!data->mm)
    {
      wys_error ("Error creating ModemManager Manager: %s",
                 error->message);
    }


  g_signal_connect_swapped (G_DBUS_OBJECT_MANAGER (data->mm),
                            "interface-added",
                            G_CALLBACK (interface_added_cb), data);
  g_signal_connect_swapped (G_DBUS_OBJECT_MANAGER (data->mm),
                            "interface-removed",
                            G_CALLBACK (interface_removed_cb), data);
  g_signal_connect_swapped (G_DBUS_OBJECT_MANAGER (data->mm),
                            "object-added",
                            G_CALLBACK (object_added_cb), data);
  g_signal_connect_swapped (G_DBUS_OBJECT_MANAGER (data->mm),
                            "object-removed",
                            G_CALLBACK (object_removed_cb), data);

  add_mm_objects (data);
}

static void
mm_appeared_cb (GDBusConnection *connection,
                const gchar *name,
                const gchar *name_owner,
                struct wys_data *data)
{
  g_debug ("ModemManager appeared on D-Bus");

  mm_manager_new (connection,
                  G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                  NULL,
                  (GAsyncReadyCallback) mm_manager_new_cb,
                  data);
}


static void
clear_dbus (struct wys_data *data)
{
  g_hash_table_remove_all (data->modems);

  g_clear_object (&data->mm);
}


void
mm_vanished_cb (GDBusConnection *connection,
                const gchar *name,
                struct wys_data *data)
{
  g_debug ("ModemManager vanished from D-Bus");
  clear_dbus (data);
}


static void
set_up (struct wys_data *data,
        const gchar *codec,
        const gchar *modem)
{
  data->audio = wys_audio_new (codec, modem);

  data->modems = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        g_free, g_object_unref);

  data->watch_id =
    g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                      MM_DBUS_SERVICE,
                      G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
                      (GBusNameAppearedCallback)mm_appeared_cb,
                      (GBusNameVanishedCallback)mm_vanished_cb,
                      data, NULL);

  g_debug ("Watching for ModemManager");
}


static void
tear_down (struct wys_data *data)
{
  clear_dbus (data);
  g_bus_unwatch_name (data->watch_id);
  g_hash_table_unref (data->modems);
  g_object_unref (G_OBJECT (data->audio));
}


static void
run (const gchar *codec,
     const gchar *modem)
{
  struct wys_data data;

  memset (&data, 0, sizeof (struct wys_data));
  set_up (&data, codec, modem);

  main_loop = g_main_loop_new (NULL, FALSE);

  printf (APPLICATION_NAME " started with codec `%s', modem `%s'\n",
          codec, modem);
  g_main_loop_run (main_loop);

  g_main_loop_unref (main_loop);
  main_loop = NULL;

  tear_down (&data);
}


static void
check_machine (const gchar *machine)
{
  gboolean ok, passed;
  GError *error = NULL;

  ok = mchk_check_machine (APP_DATA_NAME,
                           machine,
                           &passed,
                           &error);
  if (!ok)
    {
      g_warning ("Error checking machine name against"
                 " whitelist/blacklist, continuing anyway");
      g_error_free (error);
    }
  else if (!passed)
    {
      g_message ("Machine name `%s' did not pass"
                 " whitelist/blacklist check, exiting",
                 machine);
      exit (EXIT_SUCCESS);
    }
}


static void
terminate (int signal)
{
  if (main_loop)
    {
      g_main_loop_quit (main_loop);
    }
}


/** Ignore signals which make systemd refrain from restarting us due
 * to "Restart=on-failure": SIGHUP, SIGINT, and SIGPIPE.
 */
static void
setup_signals ()
{
  void (*ret)(int);

#define try_setup(NUM,handler)                          \
  ret = signal (SIG##NUM, handler);                     \
  if (ret == SIG_ERR)                                   \
    {                                                   \
      g_error ("Error setting signal handler: %s",      \
               g_strerror (errno));                     \
    }

  try_setup (HUP,  SIG_IGN);
  try_setup (INT,  SIG_IGN);
  try_setup (PIPE, SIG_IGN);
  try_setup (TERM, terminate);

#undef try_setup
}


/** This function will close @fd */
static gchar *
read_machine_conf_file (const gchar *filename,
                        int          fd)
{
  GInputStream *unix_stream;
  GDataInputStream *data_stream;
  gboolean try_again;
  gchar *line;
  GError *error = NULL;

  g_debug ("Reading machine configuration file `%s'", filename);

  unix_stream = g_unix_input_stream_new (fd, TRUE);
  g_assert (unix_stream != NULL);

  data_stream = g_data_input_stream_new (unix_stream);
  g_assert (data_stream != NULL);
  g_object_unref (unix_stream);

  do
    {
      try_again = FALSE;

      line = g_data_input_stream_read_line_utf8
        (data_stream, NULL, NULL, &error);

      if (error)
        {
          g_warning ("Error reading from machine"
                     " configuration file `%s': %s",
                     filename, error->message);
          g_error_free (error);
        }
      else if (line)
        {
          g_strstrip (line);

          // Skip comments and empty lines
          if (line[0] == '#' || line[0] == '\0')
            {
              g_free (line);
              try_again = TRUE;
            }
        }
    }
  while (try_again);

  g_object_unref (data_stream);
  return line;
}


static gchar *
dir_machine_conf (const gchar *dir,
                  const gchar *machine,
                  const gchar *key)
{
  gchar *filename;
  int fd;
  gchar *value = NULL;

  filename = g_build_filename (dir, APP_DATA_NAME,
                               "machine-conf",
                               machine, key, NULL);

  g_debug ("Trying machine configuration file `%s'",
           filename);

  fd = g_open (filename, O_RDONLY, 0);
  if (fd == -1)
    {
      if (errno != ENOENT)
        {
          // The error isn't that the file doesn't exist
          g_warning ("Error opening machine"
                     " configuration file `%s': %s",
                     filename, g_strerror (errno));
        }
    }
  else
    {
      value = read_machine_conf_file (filename, fd);
    }

  g_free (filename);
  return value;
}


static gchar *
machine_conf (const gchar *machine,
              const gchar *key)
{
  gchar *value = NULL;
  const gchar * const *dirs, * const *dir;


#define try_dir(d)                                      \
  value = dir_machine_conf (d, machine, key);           \
  if (value)                                            \
    {                                                   \
      return value;                                     \
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

  return NULL;
}


static void
ensure_alsa_card (const gchar  *machine,
                  const gchar  *var,
                  const gchar  *key,
                        gchar **name)
{
  const gchar *env;

  if (*name)
    {
      return;
    }

  env = g_getenv (var);
  if (env)
    {
      *name = g_strdup (env);
      return;
    }

  if (machine)
    {
      *name = machine_conf (machine, key);
      if (*name)
        {
          return;
        }
    }

  wys_error ("No %s specified", key);
}


int
main (int argc, char **argv)
{
  GError *error = NULL;
  GOptionContext *context;
  gboolean ok;
  g_autofree gchar *codec = NULL;
  g_autofree gchar *modem = NULL;
  g_autofree gchar *machine = NULL;

  GOptionEntry options[] =
    {
      { "codec", 'c', 0, G_OPTION_ARG_STRING, &codec, "Name of the codec's ALSA card", "NAME" },
      { "modem", 'm', 0, G_OPTION_ARG_STRING, &modem, "Name of the modem's ALSA card", "NAME" },
      { NULL }
    };

  setlocale(LC_ALL, "");

  machine = mchk_read_machine (NULL);
  if (machine)
    {
      check_machine (machine);
    }
  else
    {
      g_warning ("Could not read machine name, continuing without machine check");
    }


  context = g_option_context_new ("- set up PulseAudio loopback for phone call audio");
  g_option_context_add_main_entries (context, options, NULL);
  ok = g_option_context_parse (context, &argc, &argv, &error);
  if (!ok)
    {
      g_print ("Error parsing options: %s\n", error->message);
    }


  if (machine)
    {
      /* Convert any directory separator characters to "_" */
      g_strdelimit (machine, G_DIR_SEPARATOR_S, '_');
    }

  ensure_alsa_card (machine, "WYS_CODEC", "codec", &codec);
  ensure_alsa_card (machine, "WYS_MODEM", "modem", &modem);

  setup_signals ();

  run (codec, modem);

  return 0;
}
