/*
 * (C) Copyright 2013 Kurento (http://kurento.org/)
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

#include <gst/gst.h>
#include "commons/kmsutils.h"
#include "kmsplumberendpoint.h"
#include "kmsmultichannelcontroller.h"
#include "kms-elements-marshal.h"

#define parent_class kms_plumber_endpoint_parent_class

#define SCTP_DEFAULT_ADDR "localhost"
#define SCTP_DEFAULT_LOCAL_PORT 0
#define SCTP_DEFAULT_REMOTE_PORT 9999

#define KMS_WAIT_TIMEOUT 5

#define PLUGIN_NAME "plumberendpoint"
#define GST_CAT_DEFAULT kms_plumber_endpoint_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define KMS_PLUMBER_ENDPOINT_GET_PRIVATE(obj) (  \
  G_TYPE_INSTANCE_GET_PRIVATE (               \
    (obj),                                    \
    KMS_TYPE_PLUMBER_ENDPOINT,                \
    KmsPlumberEndpointPrivate                 \
  )                                           \
)

struct _KmsPlumberEndpointPrivate
{
  KmsMultiChannelController *mcc;

  gchar *local_addr;
  gchar *remote_addr;

  guint16 local_port;
  guint16 remote_port;

  /* SCTP server elements */
  GstElement *audiosrc;
  GstElement *videosrc;

  /* SCTP client elements */
  GstElement *audiosink;
  GstElement *videosink;
};

typedef struct _SyncCurrentPortData
{
  GCond cond;
  GMutex mutex;
  gboolean done;
  gint port;
} SyncCurrentPortData;

enum
{
  PROP_0,
  PROP_LOCAL_ADDR,
  PROP_LOCAL_PORT,
  PROP_REMOTE_ADDR,
  PROP_REMOTE_PORT,
  N_PROPERTIES
};

enum
{
  /* actions */
  ACTION_ACCEPT,
  ACTION_CONNECT,
  LAST_SIGNAL
};

static guint plumberEndPoint_signals[LAST_SIGNAL] = { 0 };

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (KmsPlumberEndpoint, kms_plumber_endpoint,
    KMS_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (kms_plumber_endpoint_debug_category, PLUGIN_NAME,
        0, "debug category for plumberendpoint element"));

static void
kms_plumber_endpoint_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  KmsPlumberEndpoint *plumberendpoint = KMS_PLUMBER_ENDPOINT (object);

  switch (property_id) {
    case PROP_LOCAL_ADDR:
      if (!g_value_get_string (value)) {
        GST_WARNING ("local-address property cannot be NULL");
        break;
      }

      g_free (plumberendpoint->priv->local_addr);
      plumberendpoint->priv->local_addr = g_value_dup_string (value);
      break;
    case PROP_LOCAL_PORT:
      plumberendpoint->priv->local_port = g_value_get_int (value);
      break;
    case PROP_REMOTE_ADDR:
      if (!g_value_get_string (value)) {
        GST_WARNING ("remote-address property cannot be NULL");
        break;
      }

      g_free (plumberendpoint->priv->remote_addr);
      plumberendpoint->priv->remote_addr = g_value_dup_string (value);
      break;
    case PROP_REMOTE_PORT:
      plumberendpoint->priv->remote_port = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
kms_plumber_endpoint_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  KmsPlumberEndpoint *plumberendpoint = KMS_PLUMBER_ENDPOINT (object);

  switch (property_id) {
    case PROP_LOCAL_ADDR:
      g_value_set_string (value, plumberendpoint->priv->local_addr);
      break;
    case PROP_LOCAL_PORT:
      g_value_set_int (value, plumberendpoint->priv->local_port);
      break;
    case PROP_REMOTE_ADDR:
      g_value_set_string (value, plumberendpoint->priv->remote_addr);
      break;
    case PROP_REMOTE_PORT:
      g_value_set_int (value, plumberendpoint->priv->remote_port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
kms_plumber_endpoint_finalize (GObject * object)
{
  KmsPlumberEndpoint *plumberendpoint = KMS_PLUMBER_ENDPOINT (object);

  g_free (plumberendpoint->priv->local_addr);
  g_free (plumberendpoint->priv->remote_addr);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
sctp_server_notify_current_port (GObject * object, GParamSpec * pspec,
    SyncCurrentPortData * syncdata)
{
  gint port;

  g_object_get (G_OBJECT (object), "current-port", &port, NULL);

  /* Wake up cause we got bound port */

  g_mutex_lock (&syncdata->mutex);
  syncdata->done = TRUE;
  syncdata->port = port;
  g_cond_signal (&syncdata->cond);
  g_mutex_unlock (&syncdata->mutex);
}

static int
kms_plumber_endpoint_create_sctp_src (StreamType type, guint16 chanid,
    KmsPlumberEndpoint * self)
{
  SyncCurrentPortData syncdata;
  GstElement *agnosticbin;
  GstElement **element;
  gint port = -1;
  gulong sig_id;

  switch (type) {
    case STREAM_TYPE_AUDIO:
      if (self->priv->audiosrc != NULL) {
        GST_WARNING ("Audio src is already created");
        return -1;
      }
      agnosticbin = kms_element_get_audio_agnosticbin (KMS_ELEMENT (self));
      self->priv->audiosrc = gst_element_factory_make ("sctpserversrc", NULL);
      element = &self->priv->audiosrc;
      break;
    case STREAM_TYPE_VIDEO:
      if (self->priv->videosrc != NULL) {
        GST_WARNING ("Video src is already created");
        return -1;
      }
      agnosticbin = kms_element_get_video_agnosticbin (KMS_ELEMENT (self));
      self->priv->videosrc = gst_element_factory_make ("sctpserversrc", NULL);
      element = &self->priv->videosrc;
      break;
    default:
      GST_WARNING_OBJECT (self, "Invalid stream type requested");
      return -1;
  }

  g_object_set (G_OBJECT (*element), "bind-address", self->priv->local_addr,
      NULL);

  g_cond_init (&syncdata.cond);
  g_mutex_init (&syncdata.mutex);
  syncdata.done = FALSE;
  syncdata.port = -1;

  sig_id = g_signal_connect (G_OBJECT (*element), "notify::current-port",
      (GCallback) sctp_server_notify_current_port, &syncdata);

  gst_bin_add (GST_BIN (self), *element);
  gst_element_sync_state_with_parent (*element);

  if (!gst_element_link (*element, agnosticbin)) {
    GST_ERROR ("Could not link %s to element %s",
        GST_ELEMENT_NAME (*element), GST_ELEMENT_NAME (agnosticbin));
    gst_element_set_state (*element, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self), *element);
    *element = NULL;

    goto end;
  }

  {
    gint64 end_time;

    /* wait for the signal emission to get the bound port */
    g_mutex_lock (&syncdata.mutex);
    end_time = g_get_monotonic_time () + KMS_WAIT_TIMEOUT * G_TIME_SPAN_SECOND;
    while (!syncdata.done) {
      if (!g_cond_wait_until (&syncdata.cond, &syncdata.mutex, end_time)) {
        GST_ERROR ("Time out expired while waiting for current-port signal");
      }
    }
    g_mutex_unlock (&syncdata.mutex);
  }

  port = syncdata.port;

end:
  g_signal_handler_disconnect (G_OBJECT (*element), sig_id);

  g_cond_clear (&syncdata.cond);
  g_mutex_clear (&syncdata.mutex);

  return port;
}

static void
kms_plumber_endpoint_create_mcc (KmsPlumberEndpoint * self)
{
  KMS_ELEMENT_LOCK (self);

  if (self->priv->mcc != NULL) {
    KMS_ELEMENT_UNLOCK (self);
    return;
  }

  GST_DEBUG ("Creating multi-channel control link");

  self->priv->mcc = kms_multi_channel_controller_new (self->priv->local_addr,
      self->priv->local_port);

  kms_multi_channel_controller_set_create_stream_callback (self->priv->mcc,
      (KmsCreateStreamFunction) kms_plumber_endpoint_create_sctp_src, self,
      NULL);

  kms_multi_channel_controller_start (self->priv->mcc);

  /* Try to connect control link if remote address is provided */
  if (self->priv->remote_addr != NULL) {
    GError *err = NULL;

    GST_DEBUG ("Connecting remote control link");

    if (!kms_multi_channel_controller_connect (self->priv->mcc,
            self->priv->remote_addr, self->priv->remote_port, &err)) {
      GST_DEBUG_OBJECT (self, "%s", err->message);
      g_error_free (err);
    }
  }

  KMS_ELEMENT_UNLOCK (self);
}

static void
kms_plumber_endpoint_link_valve (KmsPlumberEndpoint * self, GstElement * valve,
    GstElement ** sctpsink, StreamType type)
{
  GError *err = NULL;
  gint port;

  kms_plumber_endpoint_create_mcc (self);

  port = kms_multi_channel_controller_create_media_stream (self->priv->mcc,
      type, 0, &err);

  if (port < 0) {
    GST_ERROR_OBJECT (self, "%s", err->message);
    g_error_free (err);
    return;
  }

  *sctpsink = gst_element_factory_make ("sctpclientsink", NULL);
  g_object_set (G_OBJECT (*sctpsink), "host",
      self->priv->remote_addr, "port", port, NULL);

  gst_bin_add (GST_BIN (self), *sctpsink);
  gst_element_sync_state_with_parent (*sctpsink);

  if (!gst_element_link (valve, *sctpsink)) {
    GST_ERROR_OBJECT (self, "Could not link %s to element %s",
        GST_ELEMENT_NAME (valve), GST_ELEMENT_NAME (*sctpsink));
  } else {
    /* Open valve so that buffers and events can pass throug it */
    kms_utils_set_valve_drop (valve, FALSE);
  }
}

static void
kms_plumber_endpoint_audio_valve_added (KmsElement * self, GstElement * valve)
{
  KmsPlumberEndpoint *plumber = KMS_PLUMBER_ENDPOINT (self);

  kms_plumber_endpoint_link_valve (plumber, valve, &plumber->priv->audiosink,
      STREAM_TYPE_AUDIO);
}

static void
kms_plumber_endpoint_audio_valve_removed (KmsElement * self, GstElement * valve)
{
  GST_INFO ("TODO: Implement this");
}

static void
kms_plumber_endpoint_video_valve_added (KmsElement * self, GstElement * valve)
{
  KmsPlumberEndpoint *plumber = KMS_PLUMBER_ENDPOINT (self);

  kms_plumber_endpoint_link_valve (plumber, valve, &plumber->priv->videosink,
      STREAM_TYPE_VIDEO);
}

static void
kms_plumber_endpoint_video_valve_removed (KmsElement * self, GstElement * valve)
{
  GST_INFO ("TODO: Implement this");
}

static GstStateChangeReturn
kms_plumber_endpoint_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  KmsPlumberEndpoint *self = KMS_PLUMBER_ENDPOINT (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      kms_plumber_endpoint_create_mcc (self);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:{
      kms_multi_channel_controller_stop (self->priv->mcc);
      kms_multi_channel_controller_unref (self->priv->mcc);
      break;
    }
    default:
      break;
  }

  return ret;
}

static gboolean
kms_plumber_endpoint_accept (KmsPlumberEndpoint * self)
{
  /* TODO: Implement this */
  return FALSE;
}

static gboolean
kms_plumber_endpoint_connect (KmsPlumberEndpoint * self, gchar *host, guint port)
{
  /* TODO: Implement this */
  return FALSE;
}

static void
kms_plumber_endpoint_class_init (KmsPlumberEndpointClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  KmsElementClass *kms_element_class = KMS_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "PlumberEndpoint", "SCTP/Generic", "Kurento plugin plumber end point",
      "Santiago Carot-Nemesio <sancane at gmail dot com>");

  gobject_class->finalize = kms_plumber_endpoint_finalize;
  gobject_class->set_property = kms_plumber_endpoint_set_property;
  gobject_class->get_property = kms_plumber_endpoint_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCAL_ADDR,
      g_param_spec_string ("local-address", "Local Address",
          "The local address to bind the socket to",
          "localhost",
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REMOTE_ADDR,
      g_param_spec_string ("remote-address", "Remote Address",
          "The remote address to connect the socket to",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOCAL_PORT,
      g_param_spec_int ("local-port", "Local-port",
          "The port to listen to (0=random available port)", 0, G_MAXUINT16,
          SCTP_DEFAULT_LOCAL_PORT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REMOTE_PORT,
      g_param_spec_int ("remote-port", "Remote port",
          "The port to send the packets to", 0, G_MAXUINT16,
          0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /* set actions */
  plumberEndPoint_signals[ACTION_ACCEPT] =
      g_signal_new ("accept", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (KmsPlumberEndpointClass, accept),
      NULL, NULL, __kms_elements_marshal_BOOLEAN__VOID, G_TYPE_BOOLEAN,
      0, G_TYPE_NONE);

  plumberEndPoint_signals[ACTION_CONNECT] =
      g_signal_new ("connect", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (KmsPlumberEndpointClass, connect),
      NULL, NULL, __kms_elements_marshal_BOOLEAN__STRING_UINT, G_TYPE_BOOLEAN,
      2, G_TYPE_STRING, G_TYPE_UINT);

  klass->accept = kms_plumber_endpoint_accept;
  klass->connect = kms_plumber_endpoint_connect;

  kms_element_class->audio_valve_added =
      GST_DEBUG_FUNCPTR (kms_plumber_endpoint_audio_valve_added);
  kms_element_class->video_valve_added =
      GST_DEBUG_FUNCPTR (kms_plumber_endpoint_video_valve_added);
  kms_element_class->audio_valve_removed =
      GST_DEBUG_FUNCPTR (kms_plumber_endpoint_audio_valve_removed);
  kms_element_class->video_valve_removed =
      GST_DEBUG_FUNCPTR (kms_plumber_endpoint_video_valve_removed);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (kms_plumber_endpoint_change_state);

  /* Registers a private structure for the instantiatable type */
  g_type_class_add_private (klass, sizeof (KmsPlumberEndpointPrivate));
}

static void
kms_plumber_endpoint_init (KmsPlumberEndpoint * self)
{
  self->priv = KMS_PLUMBER_ENDPOINT_GET_PRIVATE (self);
}

gboolean
kms_plumber_endpoint_plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, PLUGIN_NAME, GST_RANK_NONE,
      KMS_TYPE_PLUMBER_ENDPOINT);
}
