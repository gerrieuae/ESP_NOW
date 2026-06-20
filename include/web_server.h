/**
 * @file web_server.h
 * @brief HTTP + WebSocket server interface for the ESP-NOW master dashboard.
 *
 * The server runs on port 80.  A browser connecting to the master's soft-AP
 * IP (192.168.4.1) receives a single-page dashboard that opens a WebSocket
 * to /ws and renders the live peer table as JSON pushes arrive.
 *
 * Callers must:
 *   1. Call webServerInit() once after WiFi soft-AP is up.
 *   2. Call webServerProcess() on every loop() iteration to clean up
 *      stale WebSocket client handles.
 *   3. Call webServerBroadcast() whenever the peer table changes or on a
 *      periodic heartbeat to push fresh data to all connected browsers.
 */

#pragma once

#include <stdint.h>
#include "espnow_types.h"

/**
 * @brief Starts the HTTP server and registers the WebSocket endpoint.
 *
 * Must be called after WiFi.softAP() has returned successfully.
 *
 * @param[in] staIp  STA IP address string captured at connect time, or an
 *                   empty string if no STA connection was established.
 */
void webServerInit    (const char *staIp);

/**
 * @brief Periodic maintenance – cleans up handles for disconnected clients.
 *
 * Call once per loop() iteration.  Does not block.
 */
void webServerProcess (void);

/**
 * @brief Serialises the peer table to JSON and pushes it to every connected
 *        WebSocket client.
 *
 * Safe to call at any cadence; has no effect if no clients are connected.
 *
 * @param[in] peers  Pointer to the master peer table array.
 * @param[in] count  Number of entries in peers[] (active + inactive).
 * @param[in] nowMs  Current millis() timestamp (used to compute age column).
 */
void webServerBroadcast(const peer_entry_t *peers,
                         uint8_t            count,
                         uint32_t           nowMs);

//=============================================================================
