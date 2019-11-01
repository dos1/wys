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

  gchar             *codec;
  gchar             *modem;
  pa_glib_mainloop  *loop;
  pa_context        *ctx;
};

G_DEFINE_TYPE (WysAudio, wys_audio, G_TYPE_OBJECT);


enum {
  PROP_0,
  PROP_CODEC,
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
context_notify_cb (pa_context *audio, gboolean *ready)
{
  pa_context_state_t audio_state;

  audio_state = pa_context_get_state (audio);
  switch (audio_state)
    {
    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      *ready = FALSE;
      break;
    case PA_CONTEXT_FAILED:
      wys_error ("Error in PulseAudio context: %s",
                  pa_strerror (pa_context_errno (audio)));
      break;
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_READY:
      *ready = TRUE;
      break;
    }
}


static void
set_up_audio_context (WysAudio *self)
{
  pa_proplist *props;
  int err;
  static gboolean ready = FALSE;

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

  self->ctx = pa_context_new (pa_glib_mainloop_get_api (self->loop),
                              APPLICATION_NAME);
  if (!self->ctx)
    {
      wys_error ("Error creating PulseAudio context");
    }

  pa_context_set_state_callback (self->ctx,
                                 (pa_context_notify_cb_t)context_notify_cb,
                                 &ready);
  err = pa_context_connect(self->ctx, NULL, PA_CONTEXT_NOFAIL, 0);
  if (err < 0)
    {
      wys_error ("Error connecting PulseAudio context: %s",
                  pa_strerror (err));
    }

  while (!ready)
    {
      g_main_context_iteration (NULL, TRUE);
    }

  pa_context_set_state_callback (self->ctx, NULL, NULL);
}


static void
set_property (GObject      *object,
              guint         property_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  WysAudio *self = WYS_AUDIO (object);

  switch (property_id) {
  case PROP_CODEC:
    self->codec = g_value_dup_string (value);
    break;

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
  g_free (self->codec);

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

  props[PROP_CODEC] =
    g_param_spec_string ("codec",
                         _("Codec"),
                         _("The ALSA card name for the codec"),
                         "sgtl5000",
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_MODEM] =
    g_param_spec_string ("modem",
                         _("Modem"),
                         _("The ALSA card name for the modem"),
                         "SIMcom SIM7100",
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
wys_audio_init (WysAudio *self)
{
}


WysAudio *
wys_audio_new (const gchar *codec,
               const gchar *modem)
{
  return g_object_new (WYS_TYPE_AUDIO,
                       "codec", codec,
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

typedef void (*FindLoopbackCallback) (gchar *source_alsa_card,
                                      gchar *sink_alsa_card,
                                      GList *modules,
                                      gpointer userdata);


struct find_loopback_data
{
  gchar *source_alsa_card;
  gchar *sink_alsa_card;
  GCallback callback;
  gpointer userdata;
  GList *modules;
};


static struct find_loopback_data *
find_loopback_data_new (const gchar *source_alsa_card,
                        const gchar *sink_alsa_card)
{
  struct find_loopback_data *data;

  data = g_rc_box_new0 (struct find_loopback_data);
  data->source_alsa_card = g_strdup (source_alsa_card);
  data->sink_alsa_card = g_strdup (sink_alsa_card);

  return data;
}


static void
find_loopback_data_clear (struct find_loopback_data *data)
{
  FindLoopbackCallback func = (FindLoopbackCallback)data->callback;

  func (data->source_alsa_card,
        data->sink_alsa_card,
        data->modules,
        data->userdata);

  g_list_free (data->modules);
  g_free (data->sink_alsa_card);
  g_free (data->source_alsa_card);
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


/**************** Sink ****************/

static void
find_loopback_get_sink_cb (pa_context *ctx,
                           const pa_sink_info *info,
                           struct loopback_module_data *module_data)
{
  struct find_loopback_data *loopback_data = module_data->loopback_data;

  if (!info)
    {
      g_warning ("Could not get sink for module %" PRIu32
                 " owning ALSA card `%s' source output",
                 module_data->module_index,
                 loopback_data->source_alsa_card);
      goto release;
    }

  if (!props_name_alsa_card (info->proplist,
                             loopback_data->sink_alsa_card))
    {
      g_debug ("Sink %" PRIu32 " `%s' for module %" PRIu32
               " is not ALSA card `%s'",
               info->index, info->name,
               module_data->module_index,
               loopback_data->sink_alsa_card);
      goto release;
    }


  g_debug ("Loopback module %" PRIu32 " has ALSA card `%s' source"
           " and ALSA card `%s' sink",
           module_data->module_index,
           loopback_data->source_alsa_card,
           loopback_data->sink_alsa_card);

  loopback_data->modules = g_list_append
    (loopback_data->modules,
     GUINT_TO_POINTER (module_data->module_index));

 release:
  loopback_module_data_release (module_data);
}

/**************** Sink input list ****************/

static void
find_loopback_sink_input_list_cb (pa_context *ctx,
                                  const pa_sink_input_info *info,
                                  int eol,
                                  void *userdata)
{
  struct loopback_module_data *module_data = userdata;
  struct find_loopback_data *loopback_data = module_data->loopback_data;

  if (eol == -1)
    {
      wys_error ("Error listing sink inputs: %s",
                 pa_strerror (pa_context_errno (ctx)));
    }

  if (eol)
    {
      g_debug ("End of sink input list reached");
      loopback_module_data_release (module_data);
      return;
    }

  if (info->owner_module != module_data->module_index)
    {
      g_debug ("Sink input %" PRIu32 " `%s' has"
               " owner module %" PRIu32 " which does"
               " not match sought module %" PRIu32
               " for ALSA card `%s' source output",
               info->index,
               info->name,
               info->owner_module,
               module_data->module_index,
               loopback_data->source_alsa_card);
      return;
    }

  g_debug ("Checking whether sink %" PRIu32
           " for sink input %" PRIu32
           " `%s' owned by module %" PRIu32
           " has ALSA card name `%s'",
           info->sink,
           info->index,
           info->name,
           info->owner_module,
           loopback_data->sink_alsa_card);

  g_rc_box_acquire (module_data);
  get_sink (ctx,
            info->sink,
            G_CALLBACK (find_loopback_get_sink_cb),
            module_data);
}

static void
find_sink_input (pa_context *ctx,
                 struct loopback_module_data *module_data)
{
  pa_operation *op;

  op = pa_context_get_sink_input_info_list
    (ctx, find_loopback_sink_input_list_cb, module_data);

  pa_operation_unref (op);
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
                 " for ALSA card `%s' source output",
                 module_data->module_index,
                 loopback_data->source_alsa_card);
      loopback_module_data_release (module_data);
      return;
    }

  if (strcmp (info->name, "module-loopback") != 0)
    {
      g_debug ("Module %" PRIu32 " for ALSA card `%s` source output"
               " is not a loopback module",
               info->index, loopback_data->source_alsa_card);
      loopback_module_data_release (module_data);
      return;
    }


  g_debug ("Module %" PRIu32 " for ALSA card `%s' source output is a"
           " loopback module, finding sink input with matching module",
           info->index, loopback_data->source_alsa_card);

  find_sink_input (ctx, module_data);
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
                 loopback_data->source_alsa_card);
      loopback_module_data_release (module_data);
      return;
    }

  if (!props_name_alsa_card (info->proplist,
                             loopback_data->source_alsa_card))
    {
      g_debug ("Source %" PRIu32 " `%s' is not ALSA card `%s'",
               info->index, info->name,
               loopback_data->source_alsa_card);
      loopback_module_data_release (module_data);
      return;
    }


  g_debug ("Checking whether module %" PRIu32
           " for ALSA card `%s' source output"
           " is a loopback module",
           module_data->module_index,
           loopback_data->source_alsa_card);

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


/** Find any loopback module between the specified source and sink
    alsa cards.

    1. Loop through all source outputs.
      1. Skip any source output that doesn't have the specified
      alsa card as its source.
      2. Skip any source output that isn't a loopback module.
      3. Loop through all sink inputs.
        1. Skip any sink input whose module index doesn't match.
        2. Skip any sink input which doesn't have the specified alsa
        card as its sink.
        3. Found a loopback.
*/
static void
find_loopback (pa_context *ctx,
               const gchar *source_alsa_card,
               const gchar *sink_alsa_card,
               GCallback callback,
               gpointer userdata)
{
  struct find_loopback_data *data;
  pa_operation *op;

  data = find_loopback_data_new (source_alsa_card, sink_alsa_card);
  data->callback = callback;
  data->userdata = userdata;

  g_debug ("Finding ALSA card `%s' source output",
           source_alsa_card);
  op = pa_context_get_source_output_info_list
    (ctx, find_loopback_source_output_list_cb, data);

  pa_operation_unref (op);
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

typedef void (*InstantiateLoopbackCallback) (gchar *source_alsa_card,
                                             gchar *sink_alsa_card,
                                             gchar *source,
                                             gchar *sink,
                                             gpointer userdata);


struct instantiate_loopback_data
{
  pa_context *ctx;
  gchar *source_alsa_card;
  gchar *sink_alsa_card;
  gchar *media_name;
  gchar *source;
  gchar *sink;
};


static struct instantiate_loopback_data *
instantiate_loopback_data_new (pa_context *ctx,
                               const gchar *source_alsa_card,
                               const gchar *sink_alsa_card,
                               const gchar *media_name)
{
  struct instantiate_loopback_data *data;

  data = g_rc_box_new0 (struct instantiate_loopback_data);

  pa_context_ref (ctx);
  data->ctx = ctx;

  data->source_alsa_card = g_strdup (source_alsa_card);
  data->sink_alsa_card = g_strdup (sink_alsa_card);
  data->media_name = g_strdup (media_name);

  return data;
}


static void
instantiate_loopback_data_clear (struct instantiate_loopback_data *data)
{
  g_free (data->sink);
  g_free (data->source);
  g_free (data->media_name);
  g_free (data->sink_alsa_card);
  g_free (data->source_alsa_card);
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
      g_warning ("Error instantiating loopback module with source `%s'"
                 " (ALSA card `%s') and sink `%s' (ALSA card `%s'): %s",
                 data->source,
                 data->source_alsa_card,
                 data->sink,
                 data->sink_alsa_card,
                 pa_strerror (pa_context_errno (ctx)));
    }
  else
    {
      g_debug ("Instantiated loopback module %" PRIu32
               " with source `%s' (ALSA card `%s')"
               " and sink `%s' (ALSA card `%s')",
               index,
               data->source,
               data->source_alsa_card,
               data->sink,
               data->sink_alsa_card);
    }

  instantiate_loopback_data_release (data);
}


static void
instantiate_loopback_sink_cb (const gchar *alsa_card_name,
                              const gchar *pulse_object_name,
                              gpointer userdata)
{
  struct instantiate_loopback_data *data = userdata;
  pa_proplist *stream_props;
  gchar *stream_props_str;
  gchar *arg;
  pa_operation *op;

  if (!pulse_object_name)
    {
      g_warning ("Could not find sink for ALSA card `%s'",
                 alsa_card_name);
      instantiate_loopback_data_release (data);
      return;
    }

  data->sink = g_strdup (pulse_object_name);

  g_debug ("Instantiating loopback module with source `%s'"
           " (ALSA card `%s') and sink `%s' (ALSA card `%s')",
           data->source,
           data->source_alsa_card,
           data->sink,
           data->sink_alsa_card);

  stream_props = pa_proplist_new ();
  g_assert (stream_props != NULL);
  proplist_set (stream_props, "media.role", "phone");
  proplist_set (stream_props, "media.icon_name", "phone");
  proplist_set (stream_props, "media.name", data->media_name);

  stream_props_str = pa_proplist_to_string (stream_props);
  pa_proplist_free (stream_props);
  
  arg = g_strdup_printf ("source=%s"
                         " sink=%s"
                         " source_dont_move=true"
                         " sink_dont_move=true"
                         " sink_input_properties='%s'"
                         " source_output_properties='%s'",
                         data->source,
                         data->sink,
                         stream_props_str,
                         stream_props_str);
  pa_xfree (stream_props_str);

  op = pa_context_load_module (data->ctx,
                               "module-loopback",
                               arg,
                               instantiate_loopback_load_module_cb,
                               data);

  pa_operation_unref (op);
  g_free (arg);
}


static void
instantiate_loopback_source_cb (const gchar *alsa_card_name,
                                const gchar *pulse_object_name,
                                gpointer userdata)
{
  struct instantiate_loopback_data *data = userdata;

  if (!pulse_object_name)
    {
      g_warning ("Could not find source for ALSA card `%s'",
                 alsa_card_name);
      instantiate_loopback_data_release (data);
      return;
    }

  data->source = g_strdup (pulse_object_name);

  g_debug ("Finding sink for ALSA card `%s'", data->sink_alsa_card);
  find_alsa_card_sink (data->ctx,
                       data->sink_alsa_card,
                       G_CALLBACK (instantiate_loopback_sink_cb),
                       data);
}


static void
instantiate_loopback (pa_context *ctx,
                      const gchar *source_alsa_card,
                      const gchar *sink_alsa_card,
                      const gchar *media_name)
{
  struct instantiate_loopback_data *loopback_data;

  loopback_data = instantiate_loopback_data_new (ctx,
                                                 source_alsa_card,
                                                 sink_alsa_card,
                                                 media_name);

  g_debug ("Finding source for ALSA card `%s'", source_alsa_card);
  find_alsa_card_source (ctx,
                         source_alsa_card,
                         G_CALLBACK (instantiate_loopback_source_cb),
                         loopback_data);
}


/**************** Ensure loopback ****************/

struct ensure_loopback_data
{
  pa_context *ctx;
  const gchar *media_name;
};


static void
ensure_loopback_find_loopback_cb (gchar *source_alsa_card,
                                  gchar *sink_alsa_card,
                                  GList *modules,
                                  struct ensure_loopback_data *data)
{
  if (modules != NULL)
    {
      g_warning ("%u loopback module(s) for ALSA card `%s' source ->"
                 " ALSA card `%s' sink already exist",
                 g_list_length (modules),
                 source_alsa_card, sink_alsa_card);
    }
  else
    {
      g_debug ("Instantiating loopback module for ALSA card `%s' source ->"
               " ALSA card `%s' sink",
               source_alsa_card, sink_alsa_card);

      instantiate_loopback (data->ctx, source_alsa_card,
                            sink_alsa_card, data->media_name);
    }

  g_free (data);
}


static void
ensure_loopback (pa_context *ctx,
                 const gchar *source_alsa_card,
                 const gchar *sink_alsa_card,
                 const gchar *media_name)
{
  struct ensure_loopback_data *data;

  data = g_new (struct ensure_loopback_data, 1);
  data->ctx = ctx;
  // This is a static string so we don't need a copy
  data->media_name = media_name;

  find_loopback (ctx, source_alsa_card, sink_alsa_card,
                 G_CALLBACK (ensure_loopback_find_loopback_cb),
                 data);
}


void
wys_audio_ensure_loopback (WysAudio *self)
{
  ensure_loopback (self->ctx, self->modem, self->codec,
                   "Voice call audio (to speaker)");
  ensure_loopback (self->ctx, self->codec, self->modem,
                   "Voice call audio (from mic)");
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
ensure_no_loopback_find_loopback_cb (gchar *source_alsa_card,
                                     gchar *sink_alsa_card,
                                     GList *modules,
                                     pa_context *ctx)
{
  if (modules == NULL)
    {
      g_warning ("No loopback module(s) for ALSA card `%s' source ->"
                 " ALSA card `%s' sink",
                 source_alsa_card, sink_alsa_card);
      return;
    }

  g_debug ("Deinstantiating loopback modules for ALSA card `%s' source ->"
           " ALSA card `%s' sink",
           source_alsa_card, sink_alsa_card);

  g_list_foreach (modules,
                  (GFunc)ensure_no_loopback_modules_cb,
                  ctx);
}


static void
ensure_no_loopback (pa_context *ctx,
                    const gchar *source_alsa_card,
                    const gchar *sink_alsa_card)
{
  find_loopback (ctx, source_alsa_card, sink_alsa_card,
                 G_CALLBACK (ensure_no_loopback_find_loopback_cb),
                 ctx);
}


void
wys_audio_ensure_no_loopback (WysAudio *self)
{
  ensure_no_loopback (self->ctx, self->codec, self->modem);
  ensure_no_loopback (self->ctx, self->modem, self->codec);
}
