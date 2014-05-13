/*
 * (C) Copyright 2014 Kurento (http://kurento.org/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "kms-marshal.h"
#include "kmsselectablemixer.h"
#include "kmsmixerport.h"

#define PLUGIN_NAME "selectablemixer"

#define KMS_SELECTABLE_MIXER_LOCK(e) \
  (g_rec_mutex_lock (&(e)->priv->mutex))

#define KMS_SELECTABLE_MIXER_UNLOCK(e) \
  (g_rec_mutex_unlock (&(e)->priv->mutex))

GST_DEBUG_CATEGORY_STATIC (kms_selectable_mixer_debug_category);
#define GST_CAT_DEFAULT kms_selectable_mixer_debug_category

#define KMS_SELECTABLE_MIXER_GET_PRIVATE(obj) ( \
  G_TYPE_INSTANCE_GET_PRIVATE (                 \
    (obj),                                      \
    KMS_TYPE_SELECTABLE_MIXER,                  \
    KmsSelectableMixerPrivate                   \
  )                                             \
)

struct _KmsSelectableMixerPrivate
{
  GRecMutex mutex;
  GHashTable *ports;
};

typedef struct _KmsSelectableMixerPortData KmsSelectableMixerPortData;

struct _KmsSelectableMixerPortData
{
  KmsSelectableMixer *mixer;
  gint id;
  GstElement *audio_agnostic;
  GstElement *video_agnostic;
};

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsSelectableMixer, kms_selectable_mixer,
    KMS_TYPE_BASE_HUB,
    GST_DEBUG_CATEGORY_INIT (kms_selectable_mixer_debug_category, PLUGIN_NAME,
        0, "debug category for selectable_mixer element"));

enum
{
  SIGNAL_CONNECT_VIDEO,
  SIGNAL_CONNECT_AUDIO,
  SIGNAL_DISCONNECT_AUDIO,
  LAST_SIGNAL
};

static guint obj_signals[LAST_SIGNAL] = { 0 };

static void
destroy_gint (gpointer data)
{
  g_slice_free (gint, data);
}

static void
kms_selectable_mixer_port_data_destroy (gpointer data)
{
  /* TODO: */
}

static void
kms_selectable_mixer_dispose (GObject * object)
{
  KmsSelectableMixer *self = KMS_SELECTABLE_MIXER (object);

  GST_DEBUG_OBJECT (self, "dispose");

  KMS_SELECTABLE_MIXER_LOCK (self);

  if (self->priv->ports != NULL) {
    g_hash_table_remove_all (self->priv->ports);
    g_hash_table_unref (self->priv->ports);
    self->priv->ports = NULL;
  }

  KMS_SELECTABLE_MIXER_UNLOCK (self);

  G_OBJECT_CLASS (kms_selectable_mixer_parent_class)->dispose (object);
}

static void
kms_selectable_mixer_finalize (GObject * object)
{
  KmsSelectableMixer *self = KMS_SELECTABLE_MIXER (object);

  GST_DEBUG_OBJECT (self, "finalize");

  G_OBJECT_CLASS (kms_selectable_mixer_parent_class)->finalize (object);
  g_rec_mutex_clear (&self->priv->mutex);
}

static void
kms_selectable_mixer_unhandle_port (KmsBaseHub * hub, gint id)
{
  /* TODO: */
}

static gint
kms_selectable_mixer_handle_port (KmsBaseHub * hub, GstElement * mixer_port)
{
  /* TODO: */
  return -1;
}

static gboolean
kms_selectable_mixer_connect_video (KmsSelectableMixer * self, guint source,
    guint sink)
{
  /* TODO */
  return FALSE;
}

static gboolean
kms_selectable_mixer_connect_audio (KmsSelectableMixer * self, guint source,
    guint sink)
{
  /* TODO: */
  return FALSE;
}

static gboolean
kms_selectable_mixer_disconnect_audio (KmsSelectableMixer * self, guint source,
    guint sink)
{
  /* TODO: */
  return FALSE;
}

static void
kms_selectable_mixer_class_init (KmsSelectableMixerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  KmsBaseHubClass *base_hub_class = KMS_BASE_HUB_CLASS (klass);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "SelectableMixer", "Generic",
      "N to M selectable mixer that makes dispatching of "
      "media allowing to mix several audio streams",
      "Santiago Carot-Nemesio <sancane at gmail dot com>");

  klass->connect_video = GST_DEBUG_FUNCPTR (kms_selectable_mixer_connect_video);
  klass->connect_audio = GST_DEBUG_FUNCPTR (kms_selectable_mixer_connect_audio);
  klass->disconnect_audio =
      GST_DEBUG_FUNCPTR (kms_selectable_mixer_disconnect_audio);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (kms_selectable_mixer_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (kms_selectable_mixer_finalize);

  base_hub_class->handle_port =
      GST_DEBUG_FUNCPTR (kms_selectable_mixer_handle_port);
  base_hub_class->unhandle_port =
      GST_DEBUG_FUNCPTR (kms_selectable_mixer_unhandle_port);

  /* Signals initialization */
  obj_signals[SIGNAL_CONNECT_VIDEO] =
      g_signal_new ("connect-video",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (KmsSelectableMixerClass, connect_video), NULL, NULL,
      __kms_marshal_BOOLEAN__UINT_UINT, G_TYPE_BOOLEAN, 2, G_TYPE_UINT,
      G_TYPE_UINT);

  obj_signals[SIGNAL_CONNECT_AUDIO] =
      g_signal_new ("connect-audio",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (KmsSelectableMixerClass, connect_audio), NULL, NULL,
      __kms_marshal_BOOLEAN__UINT_UINT, G_TYPE_BOOLEAN, 2, G_TYPE_UINT,
      G_TYPE_UINT);

  obj_signals[SIGNAL_DISCONNECT_AUDIO] =
      g_signal_new ("disconnect-audio",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (KmsSelectableMixerClass, disconnect_audio), NULL, NULL,
      __kms_marshal_BOOLEAN__UINT_UINT, G_TYPE_BOOLEAN, 2, G_TYPE_UINT,
      G_TYPE_UINT);

  /* Registers a private structure for the instantiatable type */
  g_type_class_add_private (klass, sizeof (KmsSelectableMixerPrivate));
}

static void
kms_selectable_mixer_init (KmsSelectableMixer * self)
{
  self->priv = KMS_SELECTABLE_MIXER_GET_PRIVATE (self);
  self->priv->ports = g_hash_table_new_full (g_int_hash, g_int_equal,
      destroy_gint, kms_selectable_mixer_port_data_destroy);

  g_rec_mutex_init (&self->priv->mutex);
}

gboolean
kms_selectable_mixer_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
      KMS_TYPE_SELECTABLE_MIXER);
}
