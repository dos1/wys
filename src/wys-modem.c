/*
 * Copyright (C) 2018, 2019 Purism SPC
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
#include "wys-direction.h"
#include "util.h"
#include "enum-types.h"

#include <glib/gi18n.h>

static const gchar * const WYS_MODEM_HAS_AUDIO[] =
  {
   [WYS_DIRECTION_FROM_NETWORK] = "wys-has-audio-from-network",
   [WYS_DIRECTION_TO_NETWORK]   = "wys-has-audio-to-network"
  };

struct _WysModem
{
  GObject parent_instance;
  /** ModemManager voice proxy */
  MMModemVoice *voice;
  /** Map of D-Bus object paths to MMCall objects */
  GHashTable *calls;
  /** How many calls have audio, in each direction */
  guint audio_count[2];
};

G_DEFINE_TYPE(WysModem, wys_modem, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_VOICE,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  SIGNAL_AUDIO_PRESENT,
  SIGNAL_AUDIO_ABSENT,
  SIGNAL_LAST_SIGNAL,
};
static guint signals [SIGNAL_LAST_SIGNAL];


static gboolean
call_state_has_audio (WysDirection direction,
                      MMCallState  state)
{
  switch (state)
    {
    case MM_CALL_STATE_RINGING_OUT:
      return
        (direction == WYS_DIRECTION_FROM_NETWORK)
        ? TRUE : FALSE;
    case MM_CALL_STATE_ACTIVE:
      return TRUE;
    default:
      return FALSE;
    }
}


static void
update_audio_count (WysModem     *self,
                    WysDirection  direction,
                    gint          delta)
{
  const guint old_count = self->audio_count[direction];

  g_assert (delta >= 0 || self->audio_count[direction] > 0);

  self->audio_count[direction] += delta;

  if (self->audio_count[direction] > 0 && old_count == 0)
    {
      g_debug ("Modem `%s' audio %s now present",
               mm_modem_voice_get_path (self->voice),
               wys_direction_get_description (direction));
      g_signal_emit_by_name (self, "audio-present", direction);
    }
  else if (self->audio_count[direction] == 0 && old_count > 0)
    {
      g_debug ("Modem `%s' audio now absent",
               mm_modem_voice_get_path (self->voice));
      g_signal_emit_by_name (self, "audio-absent", direction);
    }
}


static gboolean
get_call_has_audio (MMCall       *mm_call,
                    WysDirection  direction)
{
  gpointer data;

  data = g_object_get_data (G_OBJECT (mm_call),
                            WYS_MODEM_HAS_AUDIO[direction]);

  return (gboolean)(GPOINTER_TO_UINT (data));
}


static void
set_call_has_audio (MMCall       *mm_call,
                    WysDirection  direction,
                    gboolean      has_audio)
{
  g_object_set_data (G_OBJECT (mm_call),
                     WYS_MODEM_HAS_AUDIO[direction],
                     GUINT_TO_POINTER ((guint)has_audio));
}


static void
update_direction_state (WysModem     *self,
                        MMCall       *mm_call,
                        const gchar  *path,
                        WysDirection  direction,
                        MMCallState   old_state,
                        MMCallState   new_state)
{
  gboolean had_audio  = call_state_has_audio (direction, old_state);
  gboolean have_audio = call_state_has_audio (direction, new_state);

  if (!had_audio && have_audio)
    {
      g_debug ("Call `%s' gained audio %s", path,
               wys_direction_get_description (direction));
      update_audio_count (self, direction, +1);
    }
  else if (had_audio && !have_audio)
    {
      g_debug ("Call `%s' lost audio %s", path,
               wys_direction_get_description (direction));
      update_audio_count (self, direction, -1);
    }

  if (had_audio != have_audio)
    {
      set_call_has_audio (mm_call, direction, have_audio);
    }
}


static void
call_state_changed_cb (MmGdbusCall       *mm_gdbus_call,
                       MMCallState        old_state,
                       MMCallState        new_state,
                       MMCallStateReason  reason,
                       WysModem          *self)
{
  MMCall *mm_call = MM_CALL (mm_gdbus_call);
  const gchar * path = mm_call_get_path (mm_call);

  g_debug ("Call `%s' state changed, new: %i, old: %i",
           path, (int)new_state, (int)old_state);

  // FIXME: deal with calls being put on hold (one call goes
  // non-audio, another call goes audio after)

  update_direction_state (self, mm_call, path,
                          WYS_DIRECTION_FROM_NETWORK,
                          old_state, new_state);
  update_direction_state (self, mm_call, path,
                          WYS_DIRECTION_TO_NETWORK,
                          old_state, new_state);
}


static void
init_call_direction (WysModem     *self,
                     MMCall       *mm_call,
                     MMCallState   state,
                     WysDirection  direction)
{
  gboolean has_audio = call_state_has_audio (direction, state);

  set_call_has_audio (mm_call, direction, has_audio);

  if (has_audio)
    {
      update_audio_count (self, direction, +1);
    }
}


static void
add_call (WysModem *self,
          MMCall   *mm_call)
{
  MmGdbusCall *mm_gdbus_call = MM_GDBUS_CALL (mm_call);
  gchar *path;
  MMCallState state;

  g_object_ref (mm_call);
  path = mm_call_dup_path (mm_call);
  g_hash_table_insert (self->calls, path, mm_call);

  g_signal_connect (mm_gdbus_call, "state-changed",
                    G_CALLBACK (call_state_changed_cb),
                    self);

  state = mm_call_get_state (mm_call);
  init_call_direction (self, mm_call, state,
                       WYS_DIRECTION_FROM_NETWORK);
  init_call_direction (self, mm_call, state,
                       WYS_DIRECTION_TO_NETWORK);

  g_debug ("Call `%s' added, state: %i", path, (int)state);
}


struct WysModemCallAddedData
{
  WysModem *self;
  gchar *path;
};


static void
call_added_list_calls_cb (MMModemVoice                 *voice,
                          GAsyncResult                 *res,
                          struct WysModemCallAddedData *data)
{
  GList *calls;
  GError *error = NULL;

  calls = mm_modem_voice_list_calls_finish (voice, res, &error);
  if (!calls)
    {
      if (error)
        {
          g_warning ("Error listing calls on MMModemVoice"
                     " after call-added signal: %s",
                     error->message);
          g_error_free (error);
        }
      else
        {
          g_warning ("No calls on MMModemVoice"
                     " after call-added signal");
        }
    }
  else
    {
      GList *node;
      MMCall *call;
      gboolean found = FALSE;

      for (node = calls; node; node = node->next)
        {
          call = MM_CALL (node->data);

          if (g_strcmp0 (mm_call_get_path (call), data->path) == 0)
            {
              add_call (data->self, call);
              found = TRUE;
            }
        }

      if (!found)
        {
          g_warning ("Could not find new call `%s' in call list"
                     " on MMModemVoice after call-added signal",
                     data->path);
        }

      g_list_free_full (calls, g_object_unref);
    }

  g_free (data->path);
  g_free (data);
}


static void
call_added_cb (MMModemVoice  *voice,
               gchar         *path,
               WysModem      *self)
{
  struct WysModemCallAddedData *data;

  if (g_hash_table_contains (self->calls, path))
    {
      g_warning ("Received call-added signal for"
                 " existing call object path `%s'", path);
      return;
    }

  data = g_new0 (struct WysModemCallAddedData, 1);
  data->self = self;
  data->path = g_strdup (path);

  mm_modem_voice_list_calls
    (voice,
     NULL,
     (GAsyncReadyCallback) call_added_list_calls_cb,
     data);
}


static void
clear_call_direction (WysModem     *self,
                      MMCall       *mm_call,
                      WysDirection  direction)
{
  gboolean has_audio =
    get_call_has_audio (mm_call, direction);

  if (has_audio)
    {
      update_audio_count (self, direction, -1);
    }
}


static void
call_deleted_cb (MMModemVoice *voice,
                 const gchar  *path,
                 WysModem     *self)
{
  MMCall *mm_call;

  g_debug ("Removing call `%s'", path);

  mm_call = g_hash_table_lookup (self->calls, path);
  if (!mm_call)
    {
      g_warning ("Could not find removed call `%s'", path);
      return;
    }

  clear_call_direction (self, mm_call,
                        WYS_DIRECTION_FROM_NETWORK);
  clear_call_direction (self, mm_call,
                        WYS_DIRECTION_TO_NETWORK);

  g_hash_table_remove (self->calls, path);

  g_debug ("Call `%s' removed", path);
}


static void
list_calls_cb (MMModemVoice  *voice,
               GAsyncResult  *res,
               WysModem *self)
{
  GList *calls, *node;
  GError *error = NULL;

  calls = mm_modem_voice_list_calls_finish (voice, res, &error);
  if (!calls)
    {
      if (error)
        {
          g_warning ("Error listing calls on MMModemVoice: %s",
                     error->message);
          g_error_free (error);
        }
      return;
    }

  for (node = calls; node; node = node->next)
    {
      add_call (self, MM_CALL (node->data));
    }

  g_list_free_full (calls, g_object_unref);
}


static void
set_property (GObject      *object,
              guint         property_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  WysModem *self = WYS_MODEM (object);

  switch (property_id) {
  case PROP_VOICE:
    g_set_object (&self->voice, g_value_get_object(value));
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
constructed (GObject *object)
{
  GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
  WysModem *self = WYS_MODEM (object);
  MmGdbusModemVoice *gdbus_voice;

  gdbus_voice = MM_GDBUS_MODEM_VOICE (self->voice);
  g_signal_connect (gdbus_voice, "call-added",
                    G_CALLBACK (call_added_cb), self);
  g_signal_connect (gdbus_voice, "call-deleted",
                    G_CALLBACK (call_deleted_cb), self);

  mm_modem_voice_list_calls
    (self->voice,
     NULL,
     (GAsyncReadyCallback) list_calls_cb,
     self);

  parent_class->constructed (object);
}


static void
dispose (GObject *object)
{
  GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
  WysModem *self = WYS_MODEM (object);

  if (g_hash_table_size (self->calls) > 0)
    {
      g_hash_table_remove_all (self->calls);
      if (self->audio_count[WYS_DIRECTION_FROM_NETWORK] > 0 ||
          self->audio_count[WYS_DIRECTION_TO_NETWORK] > 0)
        {
          self->audio_count[WYS_DIRECTION_FROM_NETWORK] =
            self->audio_count[WYS_DIRECTION_TO_NETWORK] = 0;
          g_signal_emit_by_name (self, "audio-absent");
        }
    }

  g_clear_object (&self->voice);

  parent_class->dispose (object);
}


static void
finalize (GObject *object)
{
  GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
  WysModem *self = WYS_MODEM (object);

  g_hash_table_unref (self->calls);

  parent_class->finalize (object);
}


static void
wys_modem_class_init (WysModemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = set_property;
  object_class->constructed = constructed;
  object_class->dispose = dispose;
  object_class->finalize = finalize;
  props[PROP_VOICE] =
    g_param_spec_object ("voice",
                         _("Voice object"),
                         _("A libmm-glib proxy object for the modem's voice interface"),
                         MM_TYPE_MODEM_VOICE,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);


  /**
   * WysModem::audio-present:
   * @self: The #WysModem instance.
   *
   * This signal is emitted when a modem's call enters a state where
   * it has audio in a particular direction with respect to the phone
   * network.
   */
  signals[SIGNAL_AUDIO_PRESENT] =
    g_signal_new ("audio-present",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  WYS_TYPE_DIRECTION);

  /**
   * WysModem::audio-absent:
   * @self: The #WysModem instance.
   *
   * This signal is emitted when none of the modem's calls are in a
   * state where they have audio.
   */
  signals[SIGNAL_AUDIO_ABSENT] =
    g_signal_new ("audio-absent",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  WYS_TYPE_DIRECTION);
}


static void
wys_modem_init (WysModem *self)
{
  self->calls = g_hash_table_new_full (g_str_hash, g_str_equal,
                                       g_free, g_object_unref);
}


WysModem *
wys_modem_new (MMModemVoice *voice)
{
  return g_object_new (WYS_TYPE_MODEM,
                       "voice", voice,
                       NULL);
}
