/*
 * copyright (C) 2018, 2019 Purism SPC
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

#include "wys-audio.h"
#include "util.h"

#include <glib/gi18n.h>
#include <glib-object.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>


struct _WysAudio
{
  GObject parent_instance;

  gchar             *modem;
  pa_glib_mainloop  *loop;
  pa_context        *ctx;
  gboolean           ready;
};

G_DEFINE_TYPE (WysAudio, wys_audio, G_TYPE_OBJECT);


enum {
  PROP_0,
  PROP_MODEM,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];


static void
proplist_set (pa_proplist *props,
              const char *key,
              const char *value)
{
  int err = pa_proplist_sets (props, key, value);
  if (err != 0)
    {
      wys_error ("Error setting PulseAudio property list"
                 " property: %s", pa_strerror (err));
    }
}


static void
get_card_info_cb (pa_context *audio,
                  const pa_card_info *info,
                  int is_last, void *userdata)
{
  WysAudio *self = WYS_AUDIO (userdata);
  const char *class, *alsa_card;

  if (is_last < 0)
    {
      g_warning ("Failed to get card information: %s",
                 pa_strerror (pa_context_errno (self->ctx)));
      return;
    }

  if (is_last)
    return;

  class = pa_proplist_gets (info->proplist, "device.class");
  if (g_strcmp0 (class, "modem"))
    return;

  alsa_card = pa_proplist_gets (info->proplist, "alsa.card_name");
  if (!alsa_card)
    return;

  g_debug ("Found card '%s', alsa: '%s'", info->name, alsa_card);
  self->modem = g_strdup (alsa_card);
}


static void
context_notify_cb (pa_context *audio, WysAudio *self)
{
  pa_context_state_t audio_state;
  pa_operation *op;

  audio_state = pa_context_get_state (audio);
  switch (audio_state)
    {
    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      self->ready = FALSE;
      break;
    case PA_CONTEXT_FAILED:
      wys_error ("Error in PulseAudio context: %s",
                  pa_strerror (pa_context_errno (audio)));
      break;
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_READY:
      self->ready = TRUE;
      /* Find modem if there's none already */
      if (!self->modem) {
        op = pa_context_get_card_info_list(self->ctx, get_card_info_cb, self);
        if (op)
          pa_operation_unref(op);
      }
      break;
    }
}


static void
set_up_audio_context (WysAudio *self)
{
  pa_proplist *props;
  int err;

  /* Meta data */
  props = pa_proplist_new ();
  g_assert (props != NULL);

  proplist_set (props, PA_PROP_APPLICATION_NAME, APPLICATION_NAME);
  proplist_set (props, PA_PROP_APPLICATION_ID, APPLICATION_ID);

  self->loop = pa_glib_mainloop_new (NULL);
  if (!self->loop)
    {
      wys_error ("Error creating PulseAudio main loop");
    }

  self->ctx = pa_context_new_with_proplist (pa_glib_mainloop_get_api (self->loop),
                                            APPLICATION_NAME, props);
  if (!self->ctx)
    {
      wys_error ("Error creating PulseAudio context");
    }

  pa_context_set_state_callback (self->ctx,
                                 (pa_context_notify_cb_t)context_notify_cb,
                                 self);
  err = pa_context_connect(self->ctx, NULL, PA_CONTEXT_NOFAIL, 0);
  if (err < 0)
    {
      wys_error ("Error connecting PulseAudio context: %s",
                  pa_strerror (err));
    }

  while (!self->ready)
    {
      g_main_context_iteration (NULL, TRUE);
    }

  pa_context_set_state_callback (self->ctx, NULL, NULL);
  pa_proplist_free (props);
}


static void
set_property (GObject      *object,
              guint         property_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  WysAudio *self = WYS_AUDIO (object);

  switch (property_id) {
  case PROP_MODEM:
    self->modem = g_value_dup_string (value);
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
  WysAudio *self = WYS_AUDIO (object);

  set_up_audio_context (self);

  parent_class->constructed (object);
}


static void
dispose (GObject *object)
{
  GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
  WysAudio *self = WYS_AUDIO (object);

  if (self->ctx)
    {
      pa_context_disconnect (self->ctx);
      pa_context_unref (self->ctx);
      self->ctx = NULL;

      pa_glib_mainloop_free (self->loop);
      self->loop = NULL;
    }


  parent_class->dispose (object);
}


static void
finalize (GObject *object)
{
  GObjectClass *parent_class = g_type_class_peek (G_TYPE_OBJECT);
  WysAudio *self = WYS_AUDIO (object);

  g_free (self->modem);

  parent_class->finalize (object);
}


static void
wys_audio_class_init (WysAudioClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = set_property;
  object_class->constructed  = constructed;
  object_class->dispose      = dispose;
  object_class->finalize     = finalize;

  props[PROP_MODEM] =
    g_param_spec_string ("modem",
                         _("Modem"),
                         _("The ALSA card name for the modem"),
                         NULL,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
wys_audio_init (WysAudio *self)
{
}


WysAudio *
wys_audio_new (const gchar *modem)
{
  return g_object_new (WYS_TYPE_AUDIO,
                       "modem", modem,
                       NULL);
}


/**************** Get single info ****************/

struct callback_data
{
  GCallback callback;
  gpointer userdata;
};


#define GET_SINGLE_INFO_CALLBACK(SingleInfo, single_info)       \
  typedef void (*Get##SingleInfo##Callback)                     \
  (pa_context *ctx,                                             \
   const pa_##single_info##_info *info,                         \
   gpointer userdata);


#define GET_SINGLE_INFO_CB(SingleInfo, single_info)             \
  static void                                                   \
  get_##single_info##_cb (pa_context *ctx,                      \
                          const pa_##single_info##_info *info,  \
                          int eol,                              \
                          void *userdata)                       \
  {                                                             \
    struct callback_data *data = userdata;                      \
    Get##SingleInfo##Callback func;                             \
                                                                \
    if (eol == -1)                                              \
      {                                                         \
        wys_error ("Error getting PulseAudio "                  \
                   #single_info ": %s",                         \
                   pa_strerror (pa_context_errno (ctx)));       \
      }                                                         \
                                                                \
    if (eol)                                                    \
      {                                                         \
        if (data->callback)                                     \
          {                                                     \
            func = (Get##SingleInfo##Callback)data->callback;   \
            func (ctx, NULL, data->userdata);                   \
          }                                                     \
                                                                \
        g_free (data);                                          \
        return;                                                 \
      }                                                         \
                                                                \
    g_assert (info != NULL);                                    \
                                                                \
    /* We should only be called once with data */               \
    g_assert (data->callback != NULL);                          \
                                                                \
    func = (Get##SingleInfo##Callback)data->callback;           \
    func (ctx, info, data->userdata);                           \
                                                                \
    data->callback = NULL;                                      \
  }


#define GET_SINGLE_INFO(single_info, single_info_func)  \
  static void                                           \
  get_##single_info (pa_context *ctx,                   \
                     uint32_t index,                    \
                     GCallback callback,                \
                     gpointer userdata)                 \
  {                                                     \
    pa_operation *op;                                   \
    struct callback_data *data;                         \
                                                        \
    data = g_new (struct callback_data, 1);             \
    data->callback = callback;                          \
    data->userdata = userdata;                          \
                                                        \
    op = pa_context_get_##single_info_func              \
      (ctx,                                             \
       index,                                           \
       get_##single_info##_cb,                          \
       data);                                           \
                                                        \
    pa_operation_unref (op);                            \
  }


#define DECLARE_GET_SINGLE_INFO(SingleInfo, single_info, single_info_func) \
  GET_SINGLE_INFO_CALLBACK(SingleInfo, single_info)                     \
  GET_SINGLE_INFO_CB(SingleInfo, single_info)                           \
  GET_SINGLE_INFO(single_info, single_info_func)


DECLARE_GET_SINGLE_INFO(Source, source, source_info_by_index);
DECLARE_GET_SINGLE_INFO(Module, module, module_info);
DECLARE_GET_SINGLE_INFO(Sink,   sink,   sink_info_by_index);


/**************** PulseAudio properties ****************/

static gboolean
prop_matches (pa_proplist *props,
              const gchar *key,
              const gchar *needle)
{
  const char *value;

  value = pa_proplist_gets (props, key);

  if (!value)
    {
      return FALSE;
    }

  return strcmp (value, needle) == 0;
}


static gboolean
props_name_alsa_card (pa_proplist *props,
                      const gchar *alsa_card)
{
#define check(key,val)                          \
  if (!prop_matches (props, key, val))          \
    {                                           \
      return FALSE;                             \
    }

  check ("device.class",   "sound");
  check ("device.api",     "alsa");
  check ("alsa.card_name", alsa_card);

#undef check

  return TRUE;
}


/**************** Find loopback data ****************/

typedef void (*FindLoopbackCallback) (gchar *alsa_card,
                                      WysDirection direction,
                                      GList *modules,
                                      gpointer userdata);


struct find_loopback_data
{
  gchar *alsa_card;
  WysDirection direction;
  GCallback callback;
  gpointer userdata;
  GList *modules;
};


static struct find_loopback_data *
find_loopback_data_new (const gchar *alsa_card,
                        WysDirection direction)
{
  struct find_loopback_data *data;

  data = g_rc_box_new0 (struct find_loopback_data);
  data->alsa_card = g_strdup (alsa_card);
  data->direction = direction;

  return data;
}


static void
find_loopback_data_clear (struct find_loopback_data *data)
{
  FindLoopbackCallback func = (FindLoopbackCallback)data->callback;

  func (data->alsa_card,
        data->direction,
        data->modules,
        data->userdata);

  g_list_free (data->modules);
  g_free (data->alsa_card);
}


static inline void
find_loopback_data_release (struct find_loopback_data *data)
{
  g_rc_box_release_full (data, (GDestroyNotify)find_loopback_data_clear);
}


/**************** Loopback module data ****************/

struct loopback_module_data
{
  struct find_loopback_data *loopback_data;
  uint32_t module_index;
};


static struct loopback_module_data *
loopback_module_data_new (struct find_loopback_data *loopback_data,
                          uint32_t module_index)
{
  struct loopback_module_data * module_data;

  module_data = g_rc_box_new0 (struct loopback_module_data);
  module_data->module_index = module_index;

  g_rc_box_acquire (loopback_data);
  module_data->loopback_data = loopback_data;

  return module_data;
}


static inline void
loopback_module_data_clear (struct loopback_module_data *module_data)
{
  find_loopback_data_release (module_data->loopback_data);
}


static inline void
loopback_module_data_release (struct loopback_module_data *module_data)
{
  g_rc_box_release_full (module_data,
                         (GDestroyNotify)loopback_module_data_clear);
}


/**************** Module ****************/

static void
find_loopback_get_module_cb (pa_context *ctx,
                             const pa_module_info *info,
                             struct loopback_module_data *module_data)
{
  struct find_loopback_data *loopback_data = module_data->loopback_data;

  if (!info)
    {
      g_warning ("Could not get module %" PRIu32
                 " for ALSA card `%s' %s",
                 module_data->module_index,
                 loopback_data->alsa_card,
                 loopback_data->direction == WYS_DIRECTION_FROM_NETWORK ? "source output" : "sink input");
      loopback_module_data_release (module_data);
      return;
    }

  if (strcmp (info->name, "module-loopback") != 0)
    {
      g_debug ("Module %" PRIu32 " for ALSA card `%s` %s"
               " is not a loopback module",
               info->index, loopback_data->alsa_card,
               loopback_data->direction == WYS_DIRECTION_FROM_NETWORK ? "source output" : "sink input");
      loopback_module_data_release (module_data);
      return;
    }


  g_debug ("Module %" PRIu32 " for ALSA card `%s' %s is a"
           " loopback module",
           info->index, loopback_data->alsa_card,
           loopback_data->direction == WYS_DIRECTION_FROM_NETWORK ? "source output" : "sink input");

  loopback_data->modules = g_list_append
    (loopback_data->modules,
     GUINT_TO_POINTER (module_data->module_index));

  loopback_module_data_release (module_data);
}

/**************** Sink ****************/

static void
find_loopback_get_sink_cb (pa_context *ctx,
                           const pa_sink_info *info,
                           struct loopback_module_data *module_data)
{
  struct find_loopback_data *loopback_data = module_data->loopback_data;

  if (!info)
    {
      g_warning ("Couldn't find sink for sink input"
                 " while finding ALSA card `%s' sink",
                 loopback_data->alsa_card);
      loopback_module_data_release (module_data);
      return;
    }

  if (!props_name_alsa_card (info->proplist,
                             loopback_data->alsa_card))
    {
      g_debug ("Sink %" PRIu32 " `%s' is not ALSA card `%s'",
               info->index, info->name,
               loopback_data->alsa_card);
      loopback_module_data_release (module_data);
      return;
    }


  g_debug ("Checking whether module %" PRIu32
           " for ALSA card `%s' sink input"
           " is a loopback module",
           module_data->module_index,
           loopback_data->alsa_card);

  get_module (ctx,
              module_data->module_index,
              G_CALLBACK (find_loopback_get_module_cb),
              module_data);
}

/**************** Sink input list ****************/

static void
find_loopback_sink_input_list_cb (pa_context *ctx,
                                  const pa_sink_input_info *info,
                                  int eol,
                                  void *userdata)
{
  struct find_loopback_data *loopback_data = userdata;
  struct loopback_module_data *module_data;

  if (eol == -1)
    {
      wys_error ("Error listing PulseAudio source outputs: %s",
                 pa_strerror (pa_context_errno (ctx)));
    }

  if (eol)
    {
      g_debug ("End of sink input list reached");
      find_loopback_data_release (loopback_data);
      return;
    }

  if (info->owner_module == PA_INVALID_INDEX)
    {
      g_debug ("Sink input %" PRIu32 " `%s'"
               " is not owned by a module",
               info->index, info->name);
      return;
    }

  module_data = loopback_module_data_new (loopback_data,
                                          info->owner_module);

  g_debug ("Getting sink %" PRIu32
           " of sink input %" PRIu32 " `%s'",
           info->sink, info->index, info->name);
  get_sink (ctx,
              info->sink,
              G_CALLBACK (find_loopback_get_sink_cb),
              module_data);
}

/**************** Source ****************/

static void
find_loopback_get_source_cb (pa_context *ctx,
                             const pa_source_info *info,
                             struct loopback_module_data *module_data)
{
  struct find_loopback_data *loopback_data = module_data->loopback_data;

  if (!info)
    {
      g_warning ("Couldn't find source for source output"
                 " while finding ALSA card `%s' source",
                 loopback_data->alsa_card);
      loopback_module_data_release (module_data);
      return;
    }

  if (!props_name_alsa_card (info->proplist,
                             loopback_data->alsa_card))
    {
      g_debug ("Source %" PRIu32 " `%s' is not ALSA card `%s'",
               info->index, info->name,
               loopback_data->alsa_card);
      loopback_module_data_release (module_data);
      return;
    }


  g_debug ("Checking whether module %" PRIu32
           " for ALSA card `%s' source output"
           " is a loopback module",
           module_data->module_index,
           loopback_data->alsa_card);

  get_module (ctx,
              module_data->module_index,
              G_CALLBACK (find_loopback_get_module_cb),
              module_data);
}


/**************** Find loopback (source output list) ****************/

static void
find_loopback_source_output_list_cb (pa_context *ctx,
                                     const pa_source_output_info *info,
                                     int eol,
                                     void *userdata)
{
  struct find_loopback_data *loopback_data = userdata;
  struct loopback_module_data *module_data;

  if (eol == -1)
    {
      wys_error ("Error listing PulseAudio source outputs: %s",
                 pa_strerror (pa_context_errno (ctx)));
    }

  if (eol)
    {
      g_debug ("End of source output list reached");
      find_loopback_data_release (loopback_data);
      return;
    }

  if (info->owner_module == PA_INVALID_INDEX)
    {
      g_debug ("Source output %" PRIu32 " `%s'"
               " is not owned by a module",
               info->index, info->name);
      return;
    }

  module_data = loopback_module_data_new (loopback_data,
                                          info->owner_module);

  g_debug ("Getting source %" PRIu32
           " of source output %" PRIu32 " `%s'",
           info->source, info->index, info->name);
  get_source (ctx,
              info->source,
              G_CALLBACK (find_loopback_get_source_cb),
              module_data);
}


/** Find any loopback module for the specified source or sink
    alsa card.

    1. Loop through all source outputs / sink outputs.
      1. Skip any source output / sink input that doesn't have the specified
      alsa card as its source.
      2. Skip any source output / sink input that isn't a loopback module.
      3. Found a loopback.
*/
static void
find_loopback (pa_context *ctx,
               const gchar *alsa_card,
               WysDirection direction,
               GCallback callback,
               gpointer userdata)
{
  struct find_loopback_data *data;
  pa_operation *op;

  data = find_loopback_data_new (alsa_card, direction);
  data->callback = callback;
  data->userdata = userdata;

  switch (direction)
    {
    case WYS_DIRECTION_FROM_NETWORK:
      g_debug ("Finding ALSA card `%s' source output",
               alsa_card);
      op = pa_context_get_source_output_info_list
        (ctx, find_loopback_source_output_list_cb, data);
      pa_operation_unref (op);
      break;
    case WYS_DIRECTION_TO_NETWORK:
      g_debug ("Finding ALSA card `%s' sink input",
               alsa_card);
      op = pa_context_get_sink_input_info_list
        (ctx, find_loopback_sink_input_list_cb, data);
      pa_operation_unref (op);
      break;
    default:
      break;
    }
}


/**************** Find ALSA card data ****************/

typedef void (*FindALSACardCallback) (const gchar *alsa_card_name,
                                      const gchar *pulse_object_name,
                                      gpointer userdata);

struct find_alsa_card_data
{
  gchar *alsa_card_name;
  GCallback callback;
  gpointer userdata;
  gchar *pulse_object_name;
};


static struct find_alsa_card_data *
find_alsa_card_data_new (const gchar *alsa_card_name)
{
  struct find_alsa_card_data *data;

  data = g_rc_box_new0 (struct find_alsa_card_data);
  data->alsa_card_name = g_strdup (alsa_card_name);

  return data;
}


static void
find_alsa_card_data_clear (struct find_alsa_card_data *data)
{
  FindALSACardCallback func = (FindALSACardCallback)data->callback;

  func (data->alsa_card_name,
        data->pulse_object_name,
        data->userdata);

  g_free (data->pulse_object_name);
  g_free (data->alsa_card_name);
}


static inline void
find_alsa_card_data_release (struct find_alsa_card_data *data)
{
  g_rc_box_release_full (data, (GDestroyNotify)find_alsa_card_data_clear);
}


/**************** Find ALSA card ****************/

#define FIND_ALSA_CARD_LIST_CB(object_type)                             \
  static void                                                           \
  find_alsa_card_##object_type##list_cb                                 \
  (pa_context *ctx,                                                     \
   const pa_##object_type##_info *info,                                 \
   int eol,                                                             \
   void *userdata)                                                      \
  {                                                                     \
    struct find_alsa_card_data *alsa_card_data = userdata;              \
                                                                        \
    if (eol == -1)                                                      \
      {                                                                 \
        wys_error ("Error listing PulseAudio " #object_type "s: %s",    \
                   pa_strerror (pa_context_errno (ctx)));               \
      }                                                                 \
                                                                        \
    if (eol)                                                            \
      {                                                                 \
        g_debug ("End of " #object_type " list reached");               \
        find_alsa_card_data_release (alsa_card_data);                   \
        return;                                                         \
      }                                                                 \
                                                                        \
    if (alsa_card_data->pulse_object_name != NULL)                      \
      {                                                                 \
        /* Already found our object */                                  \
        g_debug ("Skipping " #object_type                               \
                 " %" PRIu32 " `%s'",                                   \
                 info->index, info->name);                              \
        return;                                                         \
      }                                                                 \
                                                                        \
    g_assert (info != NULL);                                            \
                                                                        \
    if (!props_name_alsa_card (info->proplist,                          \
                               alsa_card_data->alsa_card_name))         \
      {                                                                 \
        g_debug ("The " #object_type " %" PRIu32                        \
                 " `%s' is not ALSA card `%s'",                         \
                 info->index,                                           \
                 info->name,                                            \
                 alsa_card_data->alsa_card_name);                       \
        return;                                                         \
      }                                                                 \
                                                                        \
    g_debug ("The " #object_type " %" PRIu32                            \
             " `%s' is ALSA card `%s'",                                 \
             info->index,                                               \
             info->name,                                                \
             alsa_card_data->alsa_card_name);                           \
    alsa_card_data->pulse_object_name = g_strdup (info->name);          \
  }


#define FIND_ALSA_CARD(object_type)                             \
  static void                                                   \
  find_alsa_card_##object_type (pa_context *ctx,                \
                                const gchar *alsa_card_name,    \
                                GCallback callback,             \
                                gpointer userdata)              \
  {                                                             \
    pa_operation *op;                                           \
    struct find_alsa_card_data *data;                           \
                                                                \
    data = find_alsa_card_data_new (alsa_card_name);            \
    data->callback = callback;                                  \
    data->userdata = userdata;                                  \
                                                                \
    op = pa_context_get_##object_type##_info_list               \
      (ctx, find_alsa_card_##object_type##list_cb, data);       \
                                                                \
    pa_operation_unref (op);                                    \
  }

#define DECLARE_FIND_ALSA_CARD(object_type)     \
  FIND_ALSA_CARD_LIST_CB(object_type)           \
  FIND_ALSA_CARD(object_type)


DECLARE_FIND_ALSA_CARD(source);
DECLARE_FIND_ALSA_CARD(sink);


/**************** Instantiate loopback data ****************/

typedef void (*InstantiateLoopbackCallback) (gchar *alsa_card,
                                             WysDirection direction,
                                             gchar *master,
                                             gpointer userdata);


struct instantiate_loopback_data
{
  pa_context *ctx;
  gchar *alsa_card;
  WysDirection direction;
  gchar *media_name;
  gchar *master;
};


static struct instantiate_loopback_data *
instantiate_loopback_data_new (pa_context *ctx,
                               const gchar *alsa_card,
                               WysDirection direction,
                               const gchar *media_name)
{
  struct instantiate_loopback_data *data;

  data = g_rc_box_new0 (struct instantiate_loopback_data);

  pa_context_ref (ctx);
  data->ctx = ctx;

  data->alsa_card = g_strdup (alsa_card);
  data->direction = direction;
  data->media_name = g_strdup (media_name);

  return data;
}


static void
instantiate_loopback_data_clear (struct instantiate_loopback_data *data)
{
  g_free (data->master);
  g_free (data->media_name);
  g_free (data->alsa_card);
  pa_context_unref (data->ctx);
}


static inline void
instantiate_loopback_data_release (struct instantiate_loopback_data *data)
{
  g_rc_box_release_full (data, (GDestroyNotify)instantiate_loopback_data_clear);
}


/**************** Instantiate loopback ****************/

static void
instantiate_loopback_load_module_cb (pa_context *ctx,
                                     uint32_t index,
                                     void *userdata)
{
  struct instantiate_loopback_data *data = userdata;

  if (index == PA_INVALID_INDEX)
    {
      g_warning ("Error instantiating loopback module with %s `%s'"
                 " (ALSA card `%s'): %s",
                 data->direction == WYS_DIRECTION_FROM_NETWORK ? "source" : "sink",
                 data->master,
                 data->alsa_card,
                 pa_strerror (pa_context_errno (ctx)));
    }
  else
    {
      g_debug ("Instantiated loopback module %" PRIu32
               " with %s `%s' (ALSA card `%s')",
               index,
               data->direction == WYS_DIRECTION_FROM_NETWORK ? "source" : "sink",
               data->master,
               data->alsa_card);
    }

  instantiate_loopback_data_release (data);
}


static void
instantiate_loopback_load_module (struct instantiate_loopback_data *data)
{
  pa_proplist *stream_props;
  gchar *stream_sink_props_str, *stream_source_props_str;
  gchar *arg;
  pa_operation *op;

  g_debug ("Instantiating loopback module with %s `%s'"
           " (ALSA card `%s')",
           data->direction == WYS_DIRECTION_FROM_NETWORK ? "source" : "sink",
           data->master,
           data->alsa_card);

  // sink properties
  stream_props = pa_proplist_new ();
  g_assert (stream_props != NULL);
  proplist_set (stream_props, "media.role", "phone");
  proplist_set (stream_props, "media.icon_name", "phone");
  proplist_set (stream_props, "media.name", data->media_name);
  if (data->direction == WYS_DIRECTION_FROM_NETWORK)
    {
      proplist_set (stream_props, "filter.want", "echo-cancel");
    }

  stream_sink_props_str = pa_proplist_to_string (stream_props);
  pa_proplist_free (stream_props);

  // source properties
  stream_props = pa_proplist_new ();
  g_assert (stream_props != NULL);
  proplist_set (stream_props, "media.role", "phone");
  proplist_set (stream_props, "media.icon_name", "phone");
  proplist_set (stream_props, "media.name", data->media_name);
  if (data->direction == WYS_DIRECTION_TO_NETWORK)
    {
      proplist_set (stream_props, "filter.want", "echo-cancel");
    }

  stream_source_props_str = pa_proplist_to_string (stream_props);
  pa_proplist_free (stream_props);

  arg = g_strdup_printf ("%s=%s"
                         " %s_dont_move=true"
                         " fast_adjust_threshold_msec=100"
                         " max_latency_msec=25"
                         " sink_input_properties='%s'"
                         " source_output_properties='%s'",
                         data->direction == WYS_DIRECTION_FROM_NETWORK ? "source" : "sink",
                         data->master,
                         data->direction == WYS_DIRECTION_FROM_NETWORK ? "source" : "sink",
                         stream_sink_props_str,
                         stream_source_props_str);
  pa_xfree (stream_sink_props_str);
  pa_xfree (stream_source_props_str);

  op = pa_context_load_module (data->ctx,
                               "module-loopback",
                               arg,
                               instantiate_loopback_load_module_cb,
                               data);

  pa_operation_unref (op);
  g_free (arg);
}

static void
instantiate_loopback_master_cb (const gchar *alsa_card_name,
                                const gchar *pulse_object_name,
                                gpointer userdata)
{
  struct instantiate_loopback_data *data = userdata;

  if (!pulse_object_name)
    {
      g_warning ("Could not find source/sink for ALSA card `%s'",
                 alsa_card_name);
      instantiate_loopback_data_release (data);
      return;
    }

  data->master = g_strdup (pulse_object_name);

  instantiate_loopback_load_module (data);
}


static void
instantiate_loopback (pa_context *ctx,
                      const gchar *alsa_card,
                      WysDirection direction,
                      const gchar *media_name)
{
  struct instantiate_loopback_data *loopback_data;

  loopback_data = instantiate_loopback_data_new (ctx,
                                                 alsa_card,
                                                 direction,
                                                 media_name);

  switch (direction)
    {
    case WYS_DIRECTION_FROM_NETWORK:
      g_debug ("Finding source for ALSA card `%s'", alsa_card);
      find_alsa_card_source (ctx,
                             alsa_card,
                             G_CALLBACK (instantiate_loopback_master_cb),
                             loopback_data);
      break;
    case WYS_DIRECTION_TO_NETWORK:
      g_debug ("Finding sink for ALSA card `%s'", alsa_card);
      find_alsa_card_sink (ctx,
                           alsa_card,
                           G_CALLBACK (instantiate_loopback_master_cb),
                           loopback_data);
      break;
    default:
      break;
    }
}


/**************** Ensure loopback ****************/

struct ensure_loopback_data
{
  pa_context *ctx;
  const gchar *media_name;
};


static void
ensure_loopback_find_loopback_cb (gchar *alsa_card,
                                  WysDirection direction,
                                  GList *modules,
                                  struct ensure_loopback_data *data)
{
  if (modules != NULL)
    {
      g_warning ("%u loopback module(s) for ALSA card `%s' %s ->"
                 " already exist",
                 g_list_length (modules),
                 alsa_card,
                 direction == WYS_DIRECTION_FROM_NETWORK ? "source" : "sink");
    }
  else
    {
      g_debug ("Instantiating loopback module for ALSA card `%s' %s",
               alsa_card,
               direction == WYS_DIRECTION_FROM_NETWORK ? "source" : "sink");

      instantiate_loopback (data->ctx, alsa_card,
                            direction, data->media_name);
    }

  g_free (data);
}


static void
ensure_loopback (pa_context *ctx,
                 const gchar *alsa_card,
                 WysDirection  direction,
                 const gchar *media_name)
{
  struct ensure_loopback_data *data;

  data = g_new (struct ensure_loopback_data, 1);
  data->ctx = ctx;
  // This is a static string so we don't need a copy
  data->media_name = media_name;
  find_loopback (ctx, alsa_card, direction,
                 G_CALLBACK (ensure_loopback_find_loopback_cb),
                 data);
}


void
wys_audio_ensure_loopback (WysAudio     *self,
                           WysDirection  direction)
{
  g_return_if_fail (WYS_IS_AUDIO (self));
  g_return_if_fail (self->modem);

  switch (direction)
    {
    case WYS_DIRECTION_FROM_NETWORK:
      ensure_loopback (self->ctx, self->modem, direction,
                       "Voice call audio (to speaker)");
      break;
    case WYS_DIRECTION_TO_NETWORK:
      ensure_loopback (self->ctx, self->modem, direction,
                       "Voice call audio (from mic)");
      break;
    default:
      break;
    }
}


/**************** Ensure no loopback ****************/

static void
ensure_no_loopback_unload_module_cb (pa_context *ctx,
                                     int success,
                                     void *userdata)
{
  const guint module_index = GPOINTER_TO_UINT (userdata);

  if (success)
    {
      g_debug ("Successfully deinstantiated loopback module %u",
               module_index);
    }
  else
    {
      g_warning ("Error deinstantiating loopback module %u: %s",
                 module_index,
                 pa_strerror (pa_context_errno (ctx)));
    }
}


static void
ensure_no_loopback_modules_cb (gpointer data,
                               pa_context *ctx)
{
  const uint32_t module_index = GPOINTER_TO_UINT (data);
  pa_operation *op;

  g_debug ("Deinstantiating loopback module %" PRIu32,
           module_index);

  op = pa_context_unload_module (ctx,
                                 module_index,
                                 ensure_no_loopback_unload_module_cb,
                                 data);

  pa_operation_unref (op);
}


static void
ensure_no_loopback_find_loopback_cb (gchar *alsa_card,
                                     WysDirection direction,
                                     GList *modules,
                                     pa_context *ctx)
{
  if (modules == NULL)
    {
      g_warning ("No loopback module(s) for ALSA card `%s' %s",
                 alsa_card,
                 direction == WYS_DIRECTION_FROM_NETWORK ? "source" : "sink");
      return;
    }

  g_debug ("Deinstantiating loopback modules for ALSA card `%s' %s",
           alsa_card,
           direction == WYS_DIRECTION_FROM_NETWORK ? "source" : "sink");

  g_list_foreach (modules,
                  (GFunc)ensure_no_loopback_modules_cb,
                  ctx);
}


static void
ensure_no_loopback (pa_context *ctx,
                    const gchar *alsa_card,
                    WysDirection direction)
{
  find_loopback (ctx, alsa_card, direction,
                 G_CALLBACK (ensure_no_loopback_find_loopback_cb),
                 ctx);
}


void
wys_audio_ensure_no_loopback (WysAudio     *self,
                              WysDirection  direction)
{
  g_return_if_fail (WYS_IS_AUDIO (self));
  g_return_if_fail (self->modem);

  ensure_no_loopback (self->ctx, self->modem, direction);
}
