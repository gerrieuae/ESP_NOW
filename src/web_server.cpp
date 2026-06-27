/**
 * @file web_server.cpp
 * @brief HTTP + WebSocket server implementation for the ESP-NOW master dashboard.
 *
 * Serves a single-page HTML dashboard at "/" and a WebSocket endpoint at
 * "/ws".  Peer table data is pushed as JSON to every connected browser
 * whenever webServerBroadcast() is called.
 *
 * JSON message format:
 * {
 *   "ts": <masterMillis>,
 *   "peers": [
 *     { "mac":"AA:BB:CC:DD:EE:FF", "id":2, "active":1,
 *       "ageSec":3, "analog":1024, "din":0, "uptime":120 },
 *     ...
 *   ]
 * }
 */

#include "web_server.h"
#include "gprintf.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Constants
// ============================================================================

/** @brief TCP port for the HTTP and WebSocket server. */
#define WS_HTTP_PORT      80U

/** @brief URI path for the WebSocket endpoint. */
#define WS_PATH           "/ws"

/** @brief Size of the static JSON serialisation buffer in bytes. */
#define JSON_BUF_LEN      2048U

/** @brief Bytes reserved per peer entry when sizing the JSON buffer. */
#define JSON_BYTES_PER_PEER 160U

// ============================================================================
// HTML dashboard (stored in flash via PROGMEM)
// ============================================================================

/** @brief Single-page dashboard served at "/". */
static const char HTML_PAGE[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Home Monitor</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#0d1117;color:#c9d1d9;padding:16px}
h1{color:#58a6ff;margin-bottom:4px;font-size:1.3em;letter-spacing:.5px}
p.sub{color:#8b949e;font-size:.78em;margin-bottom:14px}
table{width:100%;border-collapse:collapse;font-size:.82em}
thead tr{background:#161b22}
th{padding:7px 10px;text-align:left;color:#58a6ff;border-bottom:1px solid #30363d;white-space:nowrap}
td{padding:5px 10px;border-bottom:1px solid #21262d;white-space:nowrap}
tr.off td{color:#6e7681}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:5px;vertical-align:middle}
tr.on .dot{background:#3fb950}
tr.off .dot{background:#6e7681}
#bar{margin-top:10px;color:#8b949e;font-size:.73em;padding:4px 0}
.empty{text-align:center;color:#6e7681;padding:22px;font-style:italic}
@media(max-width:600px){th:nth-child(4),td:nth-child(4),th:nth-child(5),td:nth-child(5){display:none}}
</style>
</head>
<body>
<h1>ESP-NOW Home Monitor</h1>
<p class="sub">Master node dashboard &mdash; data updates automatically</p>
<table>
<thead>
<tr>
  <th>MAC Address</th><th>Node ID</th><th>Status</th>
  <th>Age (s)</th><th>Analog</th><th>DIN</th><th>Uptime (s)</th>
</tr>
</thead>
<tbody id="rows">
<tr><td colspan="7" class="empty">Connecting to master...</td></tr>
</tbody>
</table>
<div id="bar">Initialising...</div>
<script>
var ws, retry = 0;
function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen = function () {
    retry = 0;
    document.getElementById('bar').textContent = 'Live - connected to master';
  };
  ws.onclose = function () {
    var delay = Math.min(30, ++retry * 3);
    document.getElementById('bar').textContent = 'Disconnected - retry in ' + delay + 's';
    setTimeout(connect, delay * 1000);
  };
  ws.onerror = function () { ws.close(); };
  ws.onmessage = function (e) {
    var d;
    try { d = JSON.parse(e.data); } catch(x) { return; }
    var b = document.getElementById('rows');
    if (!d.peers || d.peers.length === 0) {
      b.innerHTML = '<tr><td colspan="7" class="empty">No slave nodes found yet - waiting for discovery...</td></tr>';
    } else {
      b.innerHTML = '';
      d.peers.forEach(function (p) {
        var cls = p.active ? 'on' : 'off';
        var din = '0x' + ('0' + p.din.toString(16).toUpperCase()).slice(-2);
        b.innerHTML +=
          '<tr class="' + cls + '">'
          + '<td><span class="dot"></span>' + p.mac + '</td>'
          + '<td>' + p.id + '</td>'
          + '<td>' + (p.active ? 'Online' : 'Offline') + '</td>'
          + '<td>' + p.ageSec + '</td>'
          + '<td>' + p.analog + '</td>'
          + '<td>' + din + '</td>'
          + '<td>' + p.uptime + '</td>'
          + '</tr>';
      });
    }
    document.getElementById('bar').textContent =
      'Last update: ' + new Date().toLocaleTimeString()
      + '  |  ' + (d.peers ? d.peers.length : 0) + ' node(s) known';
  };
}
connect();
</script>
</body>
</html>)html";

// ============================================================================
// Static server objects
// ============================================================================

/** @brief Async HTTP server instance. */
static AsyncWebServer httpServer(WS_HTTP_PORT);

/** @brief WebSocket endpoint – all peer-table pushes go through here. */
static AsyncWebSocket wsEndpoint(WS_PATH);

/** @brief Reusable JSON serialisation buffer (stack allocation avoided). */
static char jsonBuf[JSON_BUF_LEN];

// ============================================================================
// JSON helper macro
// ============================================================================

/**
 * @brief Appends a formatted string into buf at position *pos.
 *
 * Advances *pos by the number of characters written.  Silently truncates
 * if the buffer is full.  snprintf return is cast to avoid signed/unsigned
 * mismatch; the int return type is mandated by the C standard library.
 */
#define JSON_APPEND(buf, pos, bufLen, fmt, ...)                          \
    do {                                                                  \
        if ((pos) < (bufLen)) {                                           \
            int32_t _n = (int32_t)snprintf((buf) + (pos),                \
                                            (bufLen) - (pos),            \
                                            (fmt), ##__VA_ARGS__);       \
            if (_n > 0) { (pos) += (uint32_t)_n; }                      \
        }                                                                 \
    } while (0)

// ============================================================================
// Internal helpers
// ============================================================================

/**
 * @brief Formats six raw MAC bytes into a "XX:XX:XX:XX:XX:XX\0" string.
 * @param[in]  mac  6-byte MAC address.
 * @param[out] buf  Caller-provided buffer of at least 18 bytes.
 */
static void macToStr(const uint8_t *mac, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    for (uint8_t i = 0U; i < MAC_ADDR_LEN; i++)
    {
        buf[(uint8_t)(i * 3U)]      = hex[(mac[i] >> 4U) & 0x0FU];
        buf[(uint8_t)(i * 3U + 1U)] = hex[mac[i] & 0x0FU];
        buf[(uint8_t)(i * 3U + 2U)] = (i < (MAC_ADDR_LEN - 1U)) ? ':' : '\0';
    }
    buf[17] = '\0';
}

/**
 * @brief Serialises the peer table into jsonBuf as a UTF-8 JSON object.
 *
 * @param[in] peers   Peer table array.
 * @param[in] count   Number of entries (active + inactive).
 * @param[in] nowMs   Current millis() for computing per-peer age.
 * @return            Number of bytes written into jsonBuf (excluding NUL).
 */
static uint32_t buildJson(const peer_entry_t *peers,
                           uint8_t            count,
                           uint32_t           nowMs)
{
    uint32_t pos = 0U;

    JSON_APPEND(jsonBuf, pos, JSON_BUF_LEN, "{\"ts\":%lu,\"peers\":[",
                (uint32_t)nowMs);

    for (uint8_t i = 0U; i < count; i++)
    {
        char macStr[18];
        macToStr(peers[i].macAddr, macStr);

        uint32_t ageSec = (nowMs - peers[i].lastSeenMs) / 1000U;

        JSON_APPEND(jsonBuf, pos, JSON_BUF_LEN,
                    "%s{\"mac\":\"%s\",\"id\":%u,\"active\":%u,"
                    "\"ageSec\":%lu,\"analog\":%u,\"din\":%u,\"uptime\":%lu}",
                    (i > 0U) ? "," : "",
                    macStr,
                    (uint32_t)peers[i].nodeId,
                    (uint32_t)peers[i].isActive,
                    ageSec,
                    (uint32_t)peers[i].lastData.analogValue,
                    (uint32_t)peers[i].lastData.digitalInputs,
                    peers[i].lastData.uptimeSec);
    }

    JSON_APPEND(jsonBuf, pos, JSON_BUF_LEN, "]}");
    jsonBuf[pos] = '\0';
    return pos;
}

// ============================================================================
// WebSocket event handler
// ============================================================================

/**
 * @brief Handles WebSocket lifecycle events for all connected clients.
 *
 * Logs connect/disconnect; ignores data frames (the server is push-only).
 *
 * @param[in] server   WebSocket server instance.
 * @param[in] client   The client that generated the event.
 * @param[in] type     Event type (connect, disconnect, data, error, ping).
 * @param[in] arg      Event-specific argument (frame info for data events).
 * @param[in] data     Frame payload (data events only).
 * @param[in] len      Payload length.
 */
static void onWsEvent(AsyncWebSocket       *server,
                      AsyncWebSocketClient *client,
                      AwsEventType          type,
                      void                 *arg,
                      uint8_t              *data,
                      size_t                len)
{
    (void)server;
    (void)arg;
    (void)data;
    (void)len;

    switch (type)
    {
    case WS_EVT_CONNECT:
        gprintf(gDBG, "[WS] Client #%u connected from %s\r\n",
                (uint32_t)client->id(),
                client->remoteIP().toString().c_str());
        break;

    case WS_EVT_DISCONNECT:
        gprintf(gDBG, "[WS] Client #%u disconnected\r\n",
                (uint32_t)client->id());
        break;

    case WS_EVT_ERROR:
        gprintf(gDBG, "[WS] Client #%u error\r\n",
                (uint32_t)client->id());
        break;

    default:
        break;
    }
}

// ============================================================================
// HTTP root handler
// ============================================================================

/**
 * @brief Serves the embedded HTML dashboard to any GET "/" request.
 * @param[in] request  Incoming HTTP request object.
 */
static void handleRoot(AsyncWebServerRequest *request)
{
    request->send(200, "text/html", HTML_PAGE);
}

// ============================================================================
// Public API
// ============================================================================

/**
 * @brief Registers handlers and starts the HTTP server.
 *
 * Must be called after WiFi.softAP() has completed so the TCP stack is ready.
 */
void webServerInit(const char *staIp)
{
    wsEndpoint.onEvent(onWsEvent);
    httpServer.addHandler(&wsEndpoint);
    httpServer.on("/", HTTP_GET, handleRoot);
    httpServer.begin();

    IPAddress apIp = WiFi.softAPIP();
    char apIpStr[16];
    snprintf(apIpStr, sizeof(apIpStr), "%u.%u.%u.%u",
             apIp[0], apIp[1], apIp[2], apIp[3]);

    gprintf(gDBG, "[WS] HTTP server started on port %u\r\n",
            (uint32_t)WS_HTTP_PORT);
    gprintf(gDBG, "\r\n========================================\r\n");
    gprintf(gDBG, "  AP  Dashboard: http://%s/\r\n", apIpStr);
    if ((staIp != NULL) && (staIp[0] != '\0'))
    {
        gprintf(gDBG, "  STA Dashboard: http://%s/\r\n", staIp);
    }
    gprintf(gDBG, "========================================\r\n\r\n");
}

/**
 * @brief Releases memory held by handles of disconnected WebSocket clients.
 *
 * Must be called on every loop() iteration to prevent handle accumulation.
 */
void webServerProcess(void)
{
    wsEndpoint.cleanupClients();
}

/**
 * @brief Builds a JSON snapshot of the peer table and broadcasts it to every
 *        connected WebSocket client.
 *
 * @param[in] peers   Pointer to the master peer table array.
 * @param[in] count   Number of entries in the table (active + inactive).
 * @param[in] nowMs   Current millis() value for age computation.
 */
void webServerBroadcast(const peer_entry_t *peers,
                         uint8_t            count,
                         uint32_t           nowMs)
{
    if (wsEndpoint.count() == 0U)
        return; /* No connected clients – skip serialisation overhead. */

    uint32_t jsonLen = buildJson(peers, count, nowMs);
    if (jsonLen > 0U)
    {
        wsEndpoint.textAll(jsonBuf, jsonLen);
    }
}

//=============================================================================
