/*
 * (C) Copyright 2015 Kurento (http://kurento.org/)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "kmsicecandidate.h"
#include <gst/gst.h>
#include <stdlib.h>

#define GST_CAT_DEFAULT kmsicecandidate
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
#define GST_DEFAULT_NAME "kmsicecandidate"

#define kms_ice_candidate_parent_class parent_class
G_DEFINE_TYPE (KmsIceCandidate, kms_ice_candidate, G_TYPE_OBJECT);

#define KMS_ICE_CANDIDATE_GET_PRIVATE(obj) (    \
  G_TYPE_INSTANCE_GET_PRIVATE (                 \
    (obj),                                      \
    KMS_TYPE_ICE_CANDIDATE,                     \
    KmsIceCandidatePrivate                      \
  )                                             \
)

#define DEFAULT_CANDIDATE    NULL
#define DEFAULT_SDP_MID    NULL
#define DEFAULT_SDP_M_LINE_INDEX    0

#define CANDIDATE_EXPR "^(candidate:)?(?<foundation>[0-9]+) (?<cid>[0-9]+)" \
  " (?<transport>(udp|UDP|tcp|TCP)) (?<priority>[0-9]+) (?<addr>[0-9.:a-zA-Z]+)" \
  " (?<port>[0-9]+) typ (?<type>(host|srflx|prflx|relay))" \
  "( raddr (?<raddr>[0-9.:a-zA-Z]+))?" \
  "( rport (?<rport>[0-9]+))?" \
  "( tcptype (?<tcptype>(active|passive|so)))?" \
  "( generation [0-9]+)?$"

enum
{
  PROP_0,
  PROP_CANDIDATE,
  PROP_SDP_MID,
  PROP_SDP_M_LINE_INDEX,
  N_PROPERTIES
};

/* Based on http://www.w3.org/TR/webrtc/#rtcicecandidate-type */
struct _KmsIceCandidatePrivate
{
  gchar *candidate;
  gchar *sdp_mid;
  guint8 sdp_m_line_index;
  gchar *foundation;
  guint priority;
  gchar *ip;
  KmsIceProtocol protocol;
  guint port;
  KmsIceCandidateType type;
  KmsIceTcpCandidateType tcp_type;
  gchar *related_addr;          /* optional NULL if not provided */
  gint related_port;            /* optional, -1 if not provided */

  gchar *stream_id;
};

static gboolean
kms_ice_candidate_update_values (KmsIceCandidate * self)
{
  GRegex *regex;
  GMatchInfo *match_info;
  gchar *tmp = NULL;
  gboolean ret = FALSE;

  regex = g_regex_new (CANDIDATE_EXPR, 0, 0, NULL);
  g_regex_match (regex, self->priv->candidate, 0, &match_info);

  if (!g_match_info_matches (match_info)) {
    GST_WARNING_OBJECT (self, "Cannot parse from '%s'", self->priv->candidate);
    goto end;
  }

  g_free (self->priv->foundation);
  g_free (self->priv->ip);
  g_free (self->priv->related_addr);

  self->priv->ip = g_match_info_fetch_named (match_info, "addr");

  tmp = g_match_info_fetch_named (match_info, "port");
  self->priv->port = atoi (tmp);
  g_free (tmp);

  self->priv->foundation = g_match_info_fetch_named (match_info, "foundation");

  tmp = g_match_info_fetch_named (match_info, "priority");
  self->priv->priority = atoi (tmp);
  g_free (tmp);

  tmp = g_match_info_fetch_named (match_info, "transport");
  if (g_strcmp0 (tmp, "TCP") == 0 || g_strcmp0 (tmp, "tcp") == 0) {
    self->priv->protocol = KMS_ICE_PROTOCOL_TCP;
  } else if (g_strcmp0 (tmp, "UDP") == 0 || g_strcmp0 (tmp, "udp") == 0) {
    self->priv->protocol = KMS_ICE_PROTOCOL_UDP;
  } else {
    GST_ERROR_OBJECT (self, "Unsupported protocol %s", tmp);
    goto end;
  }

  g_free (tmp);

  tmp = g_match_info_fetch_named (match_info, "type");
  if (g_strcmp0 (tmp, "host") == 0) {
    self->priv->type = KMS_ICE_CANDIDATE_TYPE_HOST;
  } else if (g_strcmp0 (tmp, "srflx") == 0) {
    self->priv->type = KMS_ICE_CANDIDATE_TYPE_SRFLX;
  } else if (g_strcmp0 (tmp, "prflx") == 0) {
    self->priv->type = KMS_ICE_CANDIDATE_TYPE_PRFLX;
  } else if (g_strcmp0 (tmp, "relay") == 0) {
    self->priv->type = KMS_ICE_CANDIDATE_TYPE_RELAY;
  } else {
    GST_ERROR_OBJECT (self, "Unsupported ice candidate type %s", tmp);
    goto end;
  }

  g_free (tmp);

  tmp = g_match_info_fetch_named (match_info, "tcptype");
  if (g_strcmp0 (tmp, "active") == 0) {
    self->priv->tcp_type = KMS_ICE_TCP_CANDIDATE_TYPE_ACTIVE;
  } else if (g_strcmp0 (tmp, "passive") == 0) {
    self->priv->tcp_type = KMS_ICE_TCP_CANDIDATE_TYPE_PASSIVE;
  } else if (g_strcmp0 (tmp, "so") == 0) {
    self->priv->tcp_type = KMS_ICE_TCP_CANDIDATE_TYPE_SO;
  } else {
    self->priv->tcp_type = KMS_ICE_TCP_CANDIDATE_TYPE_NONE;
  }
  g_free (tmp);

  tmp = g_match_info_fetch_named (match_info, "raddr");
  if (tmp != NULL && g_strcmp0 (tmp, "") != 0) {
    self->priv->related_addr = tmp;
  } else {
    self->priv->related_addr = NULL;
    g_free (tmp);
  }

  tmp = g_match_info_fetch_named (match_info, "rport");
  if (tmp != NULL && g_strcmp0 (tmp, "") != 0) {
    self->priv->related_port = atoi (tmp);
  } else {
    self->priv->related_port = -1;
  }

  ret = TRUE;

end:
  g_free (tmp);

  g_match_info_free (match_info);
  g_regex_unref (regex);

  return ret;
}

static void
kms_ice_candidate_set_property (GObject * gobject, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  KmsIceCandidate *self = KMS_ICE_CANDIDATE (gobject);

  switch (property_id) {
    case PROP_CANDIDATE:{
      const gchar *str = g_value_get_string (value);

      g_free (self->priv->candidate);
      self->priv->candidate = g_strdup (str);
      kms_ice_candidate_update_values (self);
      break;
    }
    case PROP_SDP_MID:{
      const gchar *str = g_value_get_string (value);

      g_free (self->priv->sdp_mid);
      self->priv->sdp_mid = g_strdup (str);
      break;
    }
    case PROP_SDP_M_LINE_INDEX:
      self->priv->sdp_m_line_index = g_value_get_uint (value);
      break;
  }
}

static void
kms_ice_candidate_get_property (GObject * gobject, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  KmsIceCandidate *self = KMS_ICE_CANDIDATE (gobject);

  switch (property_id) {
    case PROP_CANDIDATE:
      g_value_set_string (value, self->priv->candidate);
      break;
    case PROP_SDP_MID:
      g_value_set_string (value, self->priv->sdp_mid);
      break;
    case PROP_SDP_M_LINE_INDEX:
      g_value_set_uint (value, self->priv->sdp_m_line_index);
      break;
  }
}

static void
kms_ice_candidate_finalize (GObject * gobject)
{
  KmsIceCandidate *self = KMS_ICE_CANDIDATE (gobject);

  GST_DEBUG_OBJECT (self, "finalize");

  g_free (self->priv->candidate);
  g_free (self->priv->sdp_mid);
  g_free (self->priv->stream_id);
  g_free (self->priv->foundation);
  g_free (self->priv->ip);
  g_free (self->priv->related_addr);

  G_OBJECT_CLASS (kms_ice_candidate_parent_class)->finalize (gobject);
}

static void
kms_ice_candidate_class_init (KmsIceCandidateClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = kms_ice_candidate_finalize;
  gobject_class->set_property = kms_ice_candidate_set_property;
  gobject_class->get_property = kms_ice_candidate_get_property;

  g_object_class_install_property (gobject_class, PROP_CANDIDATE,
      g_param_spec_string ("candidate",
          "ICE candidate with string representation",
          "The candidate-attribute as defined in section 15.1 of ICE (rfc5245).",
          DEFAULT_CANDIDATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SDP_MID,
      g_param_spec_string ("sdp-mid", "ID of the related m-line",
          "If present, this contains the identifier of the 'media stream identification' "
          "as defined in [RFC 3388] for the m-line this candidate is associated with.",
          DEFAULT_SDP_MID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SDP_M_LINE_INDEX,
      g_param_spec_uint ("sdp-m-line-index", "Index of the related m-line",
          "The index (starting at zero) of the m-line in the SDP this candidate is associated with.",
          0, G_MAXUINT8, DEFAULT_SDP_M_LINE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (klass, sizeof (KmsIceCandidatePrivate));
}

static void
kms_ice_candidate_init (KmsIceCandidate * self)
{
  self->priv = KMS_ICE_CANDIDATE_GET_PRIVATE (self);

  self->priv->candidate = DEFAULT_CANDIDATE;
  self->priv->sdp_mid = DEFAULT_SDP_MID;
  self->priv->sdp_m_line_index = DEFAULT_SDP_M_LINE_INDEX;
}

KmsIceCandidate *
kms_ice_candidate_new (const gchar * candidate,
    const gchar * sdp_mid, guint8 sdp_m_line_index, const gchar * stream_id)
{
  KmsIceCandidate *cand;

  cand = g_object_new (KMS_TYPE_ICE_CANDIDATE, "candidate",
      candidate, "sdp-mid", sdp_mid, "sdp-m-line-index", sdp_m_line_index,
      NULL);

  cand->priv->stream_id = g_strdup (stream_id);

  return cand;
}

const gchar *
kms_ice_candidate_get_candidate (KmsIceCandidate * self)
{
  return self->priv->candidate;
}

const gchar *
kms_ice_candidate_get_sdp_mid (KmsIceCandidate * self)
{
  return self->priv->sdp_mid;
}

guint8
kms_ice_candidate_get_sdp_m_line_index (KmsIceCandidate * self)
{
  return self->priv->sdp_m_line_index;
}

const gchar *
kms_ice_candidate_get_stream_id (KmsIceCandidate * self)
{
  return self->priv->stream_id;
}

gchar *
kms_ice_candidate_get_address (KmsIceCandidate * self)
{
  return g_strdup (self->priv->ip);
}

const guint
kms_ice_candidate_get_port (KmsIceCandidate * self)
{
  return self->priv->port;
}

int
kms_ice_candidate_get_ip_version (KmsIceCandidate * self)
{
  if (g_strstr_len (self->priv->candidate + sizeof (SDP_CANDIDATE_ATTR) + 1, -1,
          ":") == NULL) {
    return 4;
  } else {
    return 6;
  }
}

gchar *
kms_ice_candidate_get_sdp_line (KmsIceCandidate * self)
{
  return g_strdup_printf ("a=%s", self->priv->candidate);
}

gchar *
kms_ice_candidate_get_foundation (KmsIceCandidate * self)
{
  return g_strdup (self->priv->foundation);
}

guint
kms_ice_candidate_get_priority (KmsIceCandidate * self)
{
  return self->priv->priority;
}

KmsIceProtocol
kms_ice_candidate_get_protocol (KmsIceCandidate * self)
{
  return self->priv->protocol;
}

KmsIceCandidateType
kms_ice_candidate_get_candidate_type (KmsIceCandidate * self)
{
  return self->priv->type;
}

KmsIceTcpCandidateType
kms_ice_candidate_get_candidate_tcp_type (KmsIceCandidate * self)
{
  return self->priv->tcp_type;
}

gchar *
kms_ice_candidate_get_related_address (KmsIceCandidate * self)
{
  return g_strdup (self->priv->related_addr);
}

gint
kms_ice_candidate_get_related_port (KmsIceCandidate * self)
{
  return self->priv->related_port;
}

/* Utils end */

static void init_debug (void) __attribute__ ((constructor));

static void
init_debug (void)
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, GST_DEFAULT_NAME, 0,
      GST_DEFAULT_NAME);
}
