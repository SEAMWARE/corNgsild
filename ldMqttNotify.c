//
// FILE            ldMqttNotify.c
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
//
// MQTT notification delivery via libmosquitto — see header.
//
#include <stdbool.h>                                     // bool
#include <stdint.h>                                      // uint16_t
#include <string.h>                                      // strncmp, strstr, strchr, strrchr, strlen, strcpy
#include <stdio.h>                                       // snprintf
#include <stdlib.h>                                      // strtol

#include <mosquitto.h>                                   // mosquitto_*

#include "kbase/kLibLog.h"                               // kLogFunction
#include "kalloc/kaAlloc.h"                              // kaAlloc
#include "kjson/KjNode.h"                                // KjNode
#include "kjson/kjBuilder.h"                             // kjObject, kjArray, kjString, kjChildAdd
#include "kjson/kjLookup.h"                              // kjLookup
#include "kjson/kjParse.h"                               // kjParse
#include "kjson/kjRender.h"                              // kjFastRender
#include "kjson/kjRenderSize.h"                          // kjFastRenderSize

#include "swRest/swRest.h"                               // swRest

#include "swNgsild/ldMqttNotify.h"                       // Own interface


static bool mqttInitDone = false;



// -----------------------------------------------------------------------------
//
// ldMqttTlsInsecure - accept a self-signed cert on mqtts:// (default off)
//
// Off by default: an mqtts endpoint's certificate is verified against the system
// CA store. When the broker is started with --insecureNotif this is turned on,
// so an mqtts endpoint inside a trusted/firewalled network with a self-signed
// certificate is accepted (mirrors the HTTP-notification insecure mode).
//
static bool mqttTlsInsecure = false;

void ldMqttTlsInsecureSet(bool onoff)
{
  mqttTlsInsecure = onoff;
}



// -----------------------------------------------------------------------------
//
// ldMqttInit -
//
int ldMqttInit(void)
{
  if (mqttInitDone) return 0;
  if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) return -1;
  mqttInitDone = true;
  return 0;
}



// -----------------------------------------------------------------------------
//
// ldMqttCleanup -
//
void ldMqttCleanup(void)
{
  if (!mqttInitDone) return;
  mosquitto_lib_cleanup();
  mqttInitDone = false;
}



// -----------------------------------------------------------------------------
//
// ldIsMqttUri -
//
bool ldIsMqttUri(const char* uri)
{
  if (uri == NULL) return false;
  if (strncmp(uri, "mqtt://", 7)  == 0) return true;
  if (strncmp(uri, "mqtts://", 8) == 0) return true;
  return false;
}



// -----------------------------------------------------------------------------
//
// MqttUri - parsed components of an mqtt[s]:// URI.
//
// All `char*` fields point into a freshly-allocated buffer owned by
// swRest.kalloc, so no separate frees are needed.
//
typedef struct MqttUri
{
  bool         tls;
  const char*  user;       // NULL if absent
  const char*  pass;       // NULL if absent
  const char*  host;
  int          port;
  const char*  topic;      // includes any /-separated subtopics, no leading '/'
} MqttUri;



// -----------------------------------------------------------------------------
//
// parseMqttUri - decompose an mqtt[s]://[u[:p]@]host[:port]/topic URI
//
// Returns true on success. Mutates a local copy in `swRest.kalloc`; the
// out fields point into that copy and stay valid for the request.
//
static bool parseMqttUri(const char* uri, MqttUri* out)
{
  if (uri == NULL || out == NULL) return false;

  memset(out, 0, sizeof(*out));

  const char* schemeEnd;
  if (strncmp(uri, "mqtt://", 7) == 0)
  {
    out->tls   = false;
    out->port  = 1883;
    schemeEnd  = uri + 7;
  }
  else if (strncmp(uri, "mqtts://", 8) == 0)
  {
    out->tls   = true;
    out->port  = 8883;
    schemeEnd  = uri + 8;
  }
  else
    return false;

  // Working copy — we'll NUL-terminate components in place.
  int   len  = strlen(schemeEnd);
  char* buf  = (char*) kaAlloc(&swRest.kalloc, len + 1);
  memcpy(buf, schemeEnd, len + 1);

  // Split on the first '/' to separate authority from path/topic.
  char* slash = strchr(buf, '/');
  if (slash == NULL || slash[1] == '\0') return false;  // no topic
  *slash = '\0';
  out->topic = slash + 1;

  // authority = buf : optionally "user:pass@host:port"
  char* authority = buf;
  char* at = strchr(authority, '@');
  if (at != NULL)
  {
    *at = '\0';
    char* userpass = authority;
    char* colon = strchr(userpass, ':');
    if (colon != NULL)
    {
      *colon = '\0';
      out->user = userpass;
      out->pass = colon + 1;
    }
    else
    {
      out->user = userpass;
    }
    authority = at + 1;
  }

  // host[:port] (also handles bracketed IPv6 — we keep it simple here)
  char* portColon = strrchr(authority, ':');
  if (portColon != NULL && portColon[1] != '\0')
  {
    *portColon = '\0';
    long pn = strtol(portColon + 1, NULL, 10);
    if (pn > 0 && pn < 65536) out->port = (int) pn;
  }

  out->host = authority;
  if (out->host[0] == '\0') return false;

  return true;
}



// -----------------------------------------------------------------------------
//
// notifierInfoLookup - find a key (case-insensitive) in a notifierInfo array.
//
static const char* notifierInfoLookup(KjNode* arr, const char* key)
{
  if (arr == NULL || arr->type != KjArray) return NULL;
  for (KjNode* kvP = arr->value.firstChildP; kvP != NULL; kvP = kvP->next)
  {
    if (kvP->type != KjObject) continue;
    KjNode* kP = kjLookup(kvP, "key");
    KjNode* vP = kjLookup(kvP, "value");
    if (kP == NULL || kP->type != KjString)   continue;
    if (vP == NULL || vP->type != KjString)   continue;
    if (strcasecmp(kP->value.s, key) == 0)    return vP->value.s;
  }
  return NULL;
}



// -----------------------------------------------------------------------------
//
// buildMqttMessage - wrap notification body as { "metadata": {...}, "body": <notif> }
//
// Returns the rendered JSON string allocated from swRest.kalloc, or NULL.
//
static char* buildMqttMessage(const char* notifBodyJson,
                              const char* contentType,
                              const char* linkHeader,
                              KjNode*     receiverInfo)
{
  KjNode* root     = kjObject(swRest.kjsonP, NULL);
  KjNode* metadata = kjObject(swRest.kjsonP, "metadata");

  if (contentType != NULL)
    kjChildAdd(metadata, kjString(swRest.kjsonP, "Content-Type", (char*) contentType));

  if (linkHeader != NULL)
    kjChildAdd(metadata, kjString(swRest.kjsonP, "Link", (char*) linkHeader));

  // Copy receiverInfo entries into metadata as plain key/value strings.
  if (receiverInfo != NULL && receiverInfo->type == KjArray)
  {
    for (KjNode* kvP = receiverInfo->value.firstChildP; kvP != NULL; kvP = kvP->next)
    {
      if (kvP->type != KjObject) continue;
      KjNode* kP = kjLookup(kvP, "key");
      KjNode* vP = kjLookup(kvP, "value");
      if (kP == NULL || kP->type != KjString) continue;
      if (vP == NULL || vP->type != KjString) continue;
      kjChildAdd(metadata, kjString(swRest.kjsonP, kP->value.s, vP->value.s));
    }
  }

  kjChildAdd(root, metadata);

  // body: parse the notification JSON and graft as a tree.
  KjNode* bodyTree = kjParse(swRest.kjsonP, (char*) notifBodyJson);
  if (bodyTree == NULL) return NULL;
  bodyTree->name = (char*) "body";
  kjChildAdd(root, bodyTree);

  int    sz  = kjFastRenderSize(root) + 1;
  char*  buf = (char*) kaAlloc(&swRest.kalloc, sz);
  kjFastRender(root, buf);
  return buf;
}



// -----------------------------------------------------------------------------
//
// ldMqttNotify -
//
bool ldMqttNotify(const char* uri,
                  const char* notifBodyJson,
                  const char* contentType,
                  const char* linkHeader,
                  KjNode*     receiverInfo,
                  KjNode*     notifierInfo)
{
  if (!mqttInitDone && ldMqttInit() != 0) return false;

  MqttUri parsed;
  if (!parseMqttUri(uri, &parsed))
  {
    if (kLogFunction != NULL)
      kLogFunction(2, 0, __FILE__, __LINE__, __func__, "MQTT notify: invalid URI '%s'", uri);
    return false;
  }

  // QoS: notifierInfo "MQTT-QoS" or default 0.
  int qos = 0;
  const char* qosStr = notifierInfoLookup(notifierInfo, "MQTT-QoS");
  if (qosStr != NULL)
  {
    long q = strtol(qosStr, NULL, 10);
    if (q >= 0 && q <= 2) qos = (int) q;
  }

  // MQTT version: notifierInfo "MQTT-Version" or default mqtt5.0 (§ 7.2 Table 7.2-1).
  int protocolVersion = MQTT_PROTOCOL_V5;
  const char* versionStr = notifierInfoLookup(notifierInfo, "MQTT-Version");
  if (versionStr != NULL)
  {
    if      (strcasecmp(versionStr, "mqtt3.1.1") == 0) protocolVersion = MQTT_PROTOCOL_V311;
    else if (strcasecmp(versionStr, "mqtt5.0")   == 0) protocolVersion = MQTT_PROTOCOL_V5;
  }

  // Build the wrapped {metadata, body} payload.
  char* payload = buildMqttMessage(notifBodyJson, contentType, linkHeader, receiverInfo);
  if (payload == NULL) return false;
  int   payloadLen = strlen(payload);

  // mosquitto_new — clientId NULL → libmosquitto auto-generates.
  struct mosquitto* mosq = mosquitto_new(NULL, true /* clean session */, NULL);
  if (mosq == NULL) return false;

  bool ok = false;

  do
  {
    // Protocol version must be set before connect.
    if (mosquitto_int_option(mosq, MOSQ_OPT_PROTOCOL_VERSION, protocolVersion) != MOSQ_ERR_SUCCESS)
      break;

    if (parsed.user != NULL)
    {
      if (mosquitto_username_pw_set(mosq, parsed.user, parsed.pass) != MOSQ_ERR_SUCCESS)
        break;
    }

    if (parsed.tls)
    {
      // Default TLS settings — verify peer, system CAs.
      if (mosquitto_tls_set(mosq, NULL, "/etc/ssl/certs", NULL, NULL, NULL) != MOSQ_ERR_SUCCESS)
        break;

      // --insecureNotif: accept a self-signed endpoint cert (no peer/host check).
      if (mqttTlsInsecure)
      {
        mosquitto_tls_opts_set(mosq, 0 /* SSL_VERIFY_NONE */, NULL, NULL);
        mosquitto_tls_insecure_set(mosq, true);
      }
    }

    int rc = mosquitto_connect(mosq, parsed.host, parsed.port, 30 /* keepalive */);
    if (rc != MOSQ_ERR_SUCCESS) break;

    rc = mosquitto_publish(mosq, NULL /* mid */, parsed.topic,
                           payloadLen, payload, qos, false /* retain */);
    if (rc != MOSQ_ERR_SUCCESS) break;

    // Pump the loop until the outgoing data is fully written (and, for QoS 1/2,
    // the publish is acked) or we time out. mosquitto_want_write() going false
    // means the CONNECT + PUBLISH bytes have left the socket — important over
    // TLS, where the handshake spans several loop cycles, so we must NOT
    // disconnect after a single 100 ms cycle (that would drop the publish).
    int loopMs    = 5000;
    int postFlush = 0;
    while (loopMs > 0)
    {
      rc = mosquitto_loop(mosq, 100, 1);
      if (rc != MOSQ_ERR_SUCCESS) break;
      loopMs -= 100;
      if (qos == 0)
      {
        // Once the PUBLISH bytes have left the socket (want_write false), pump a
        // couple more cycles before disconnecting so the receiving broker has
        // time to route the QoS-0 message to subscribers. A single cycle is
        // enough over plain TCP but not over TLS, where the connection teardown
        // would otherwise race the broker's routing and drop the notification.
        if (!mosquitto_want_write(mosq) && (++postFlush >= 3)) { ok = true; break; }
      }
      else if (loopMs <= 4500)
      {
        ok = (rc == MOSQ_ERR_SUCCESS);
        break;
      }
    }

    mosquitto_disconnect(mosq);
  } while (0);

  mosquitto_destroy(mosq);

  if (!ok && kLogFunction != NULL)
    kLogFunction(2, 0, __FILE__, __LINE__, __func__,
                 "MQTT notify failed: host=%s port=%d topic=%s",
                 parsed.host, parsed.port, parsed.topic);

  return ok;
}
