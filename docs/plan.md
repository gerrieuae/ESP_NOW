# ESP-NOW Home Monitoring Mesh — Expansion Plan (backup copy)

Status: protocol v2 implemented in `espnow_types.h`/`main.cpp` (compiles clean);
everything else below is still design/plan. Last updated 2026-09-12.

## 1. Goal

Extend the existing ESP-NOW master/slave home-monitoring system (alarm states,
water/electricity usage) into a mesh of differently-purposed slave nodes, with
event escalation (e.g. "water hasn't stopped flowing") handled centrally by the
master, and final delivery to Home Assistant via Node-RED (WebSocket interface).

## 2. Node taxonomy

Add a `nodeType` field to discovery so the master knows each peer's capabilities:

| Node type            | Role                                                         |
|-----------------------|--------------------------------------------------------------|
| `NODE_RELAY`          | Controls one or more relays                                   |
| `NODE_DISCRETE_INPUT` | Reads digital inputs (door/window contacts, float switches)   |
| `NODE_WATER_METER`    | Optional dedicated pulse-counter for water flow (fast, cheap) |
| `NODE_BOREHOLE_CTRL`  | Borehole pump control + protection (dry-run, overcurrent)     |
| `NODE_GATEWAY`        | (only if a separate Ethernet bridge node is used, see §6)     |
| `NODE_CAMERA`         | Forked jomjol camera — sends only the final reading over ESP-NOW (see §5) |
| `NODE_REPEATER`       | Fixed one-hop relay between the camera and the master (see §5a) |

Implemented so far in `espnow_types.h`: `NODE_TYPE_UNKNOWN/MASTER/RELAY/
DISCRETE_INPUT/WATER_METER/BOREHOLE_CTRL/GATEWAY`. `NODE_TYPE_CAMERA` and
`NODE_TYPE_REPEATER` still need to be added once §5/§5a below are implemented.

## 3. Protocol redesign (small packets)

Current `espnow_packet_t` (fixed header + fixed `sensor_data_t`) doesn't scale
once payloads differ per node type. Proposed:

- `pkt_header_t` gains `nodeType` and a `protocolVersion` byte (version now,
  since this struct will keep changing).
- Replace the fixed `sensor_data_t` with a tagged variable payload:
  `uint8_t payloadLen` + `uint8_t payload[MAX_PAYLOAD]`, interpreted per
  `nodeType`/`pktType`. Keeps frames well under the ~250-byte ESP-NOW classic
  limit.
- New packet types:
  - `PKT_CMD` (master → slave): generic `{opcode, param[]}` — relay on/off,
    arm borehole, etc.
  - `PKT_CMD_ACK` (slave → master).
  - `PKT_EVENT` (slave → master, **unsolicited**, not tied to the poll cycle):
    `{eventType, severity, nodeId, timestampMs, data[]}`. Master must act on
    this immediately in the recv callback, not wait for the next poll — this
    is the mechanism for "water didn't stop" and similar alarms.
  - `PKT_EVENT_ACK` (master → slave).

The bulk-image-transfer sub-protocol (`PKT_BULK_START/CHUNK/ACK/END`) originally
planned for camera nodes is **dropped** — see §5, image transfer no longer
happens over ESP-NOW.

## 4. Master responsibilities (additions to current firmware)

- ✅ Fast-path handling for `PKT_EVENT` — implemented 2026-09-12. `PKT_EVENT`
  is dispatched from `processRxQueue()`, which already runs on every
  `loop()` iteration regardless of the master's discover/poll state, so an
  event is never held up waiting for the state machine to reach an idle
  point.
- ✅ Track `nodeType` per peer — already done as part of protocol v2.
- ✅ Per-meter watchdog — implemented 2026-09-12 as `updateValueWatchdog()`.
  **Correction from an earlier draft of this doc**, which had the logic
  backwards: the requirement is "alarm if water does **not stop**", i.e. the
  reading keeps changing continuously for too long — NOT "alarm if the
  reading stops changing" (that would flag normal idle/no-usage periods).
  Actual behaviour: each peer tracks `lastEventValue`, `activeSinceMs`
  (when the value most recently started continuously changing), and
  `valueActive`. A new `EVENT_METER_READING` (via `PKT_EVENT`,
  `event_payload_t{eventType, value}`) that differs from `lastEventValue`
  means the meter is still running; if it's been running continuously since
  `activeSinceMs` for >= `WATER_NO_STOP_TIMEOUT_MS` (fixed constant, 30 min
  default — remote tuning via `PKT_CMD` is a possible future enhancement,
  not built), the alarm is raised. A reading that repeats the previous value
  means activity has stopped, which clears the alarm and resets the timer.
  Source-agnostic: works the same regardless of which node type or path
  (repeater-relayed camera, future `NODE_WATER_METER`, etc.) produced the
  reading. On raise/clear the master immediately calls
  `webServerBroadcast()` rather than waiting for the periodic heartbeat, and
  the peer table JSON now carries an `"alarm"` field (dashboard shows an
  ALARM status + red row).
- **Master only** is the network egress point (per user constraint: "only the
  master will have the means to connect to server"). Once Ethernet-equipped
  (§6), the master is an MQTT client connecting to the broker used by
  Node-RED/Home Assistant — publishing peer telemetry/alarms/camera readings
  outward (and potentially subscribing to command topics from HA later, e.g.
  to drive relays). The existing local WS dashboard stays as-is for a browser
  connecting directly to the master; MQTT is the HA/Node-RED integration path
  (not yet built — the alarm currently only reaches the local WS dashboard).

## 5. Camera meter reading — fork jomjol/AI-on-the-edge-device to speak ESP-NOW

Investigated: https://github.com/jomjol/AI-on-the-edge-device

- Mature (5+ years, active), free, runs entirely **on-device** on ESP32-CAM /
  ESP32-S3 — TFLite inference on configurable ROIs, reads digit-wheel and
  analog-dial meters. Ships its own web UI for calibration.
- Natively publishes readings via MQTT/REST over WiFi — but the camera's
  install site is too far from any WiFi AP/router to reach a broker directly
  (user-confirmed constraint), and **only the master may reach the network**.
  So the stock networking path can't be used as-is.

**Decision (2026-09-12): fork jomjol, keep its on-device OCR and its local
calibration webpage** (ESP32 can run its own AP for on-site setup — connect a
phone/laptop directly to it, no internet needed for that), **but replace its
WiFi/MQTT publish step with a small ESP-NOW send of just the final reading**
(e.g. `{meterId, value, timestampMs}`, comfortably inside `MAX_PAYLOAD_LEN`).
ESP-NOW only needs the WiFi radio in STA mode — it does not require actually
joining an AP — so this coexists fine with running the local AP for
calibration. The reading travels camera → repeater → master (see §5a); only
the master talks MQTT/Ethernet outward. This keeps the whole design consistent
with the small-packet philosophy used everywhere else on this mesh, and drops
the previously-considered bulk-image-transfer problem entirely — no image
ever leaves the camera.

`NODE_TYPE_CAMERA` in `espnow_types.h` represents this forked device once
built.

## 5a. Repeater — fixed one-hop relay (decided: exactly one hop, not a chain)

The camera is out of ESP-NOW range of the master directly, so a repeater node
sits between them: **camera ↔ repeater ↔ master**, one hop each side, fixed
topology (not a multi-hop chain — confirmed with user, so no hop-count/TTL
field or loop-prevention logic is needed in the protocol).

Repeater behaviour: forward any frame received from its configured camera-side
MAC to the master's MAC, and any frame received from the master's MAC to the
configured camera-side MAC. Purely a MAC-layer store-and-forward proxy —
`NODE_TYPE_REPEATER` in `espnow_types.h`, no packet parsing required.

**Addressing issue — FIXED 2026-09-12.** The master's peer-table code
(`findPeer`/`addOrUpdatePeer` in `main.cpp`) previously identified peers by
their radio source MAC, which breaks once a peer is relayed (the repeater's
MAC would appear as the source instead of the camera's). Fixed:
- `findPeer(nodeId)` now searches by logical node ID (`header.senderId`),
  not MAC.
- `addOrUpdatePeer(macAddr, nodeId, nodeType)` treats `macAddr` as the
  *route* address (where to transmit to reach this peer) and refreshes it on
  every call, including for an already-known peer — so if a peer's route
  changes (e.g. starts being relayed through a repeater), the master picks
  that up automatically and keeps sending to the right place.
- `handlePollResp`/`handleDiscoverResp` now key lookups on
  `pkt->header.senderId` and pass the ESP-NOW radio source MAC through as the
  route address only.
- `peer_entry_t.macAddr`'s doc comment updated in `espnow_types.h` to reflect
  "route to reach this peer", identity is `nodeId`.
Verified with `pio run` — clean build, no warnings.

## 6. Master Ethernet uplink — reuse Alco5 / AlcoEtherServer stack

Investigated local projects: `~/Documents/PlatformIO/Projects/AlcoEtherServer`
and `alco5`, which already have a **working W6100 Ethernet integration**:

- `lib/W6100L` (`W6100L.h/.cpp`) — thin class wrapping Arduino `Ethernet.h`
  (built on the `Ethernet-master` library) for the W6100 chip: reset/CS pin
  handling, MAC/IP setup (several `begin()` overloads: DHCP w/ timeout, static
  IP, +DNS, +gateway, +subnet), connection-check helper, verbose logging via
  the same `gprintf` library already used in ESP_NOW.
- `lib/NetworkManager` (`NetworkManager.h/.cpp`) — manages WiFi *and* Ethernet
  together, with `wifiAvailable()` / `ethAvailable()` / `networkAvailable()`
  and a `getNetworkType()` — i.e. automatic fallback between the two links.
- `lib/Webpage/websocket.cpp` + `lib/webserver` (ESPAsyncWebServer/AsyncTCP) —
  working WebSocket server on top of this network stack, same pattern as
  ESP_NOW's own `web_server.cpp`.

**Decision: Option A confirmed — add W6100 Ethernet directly to the master**,
porting `W6100L` + `NetworkManager` (trimmed to what's needed) into ESP_NOW's
`lib/`, rather than standing up a second physical gateway node. The master's
existing WS server becomes reachable over Ethernet (reliable, wired) as well
as/instead of WiFi; Node-RED's `websocket-in` node points at it directly. No
second hop, no new bridging protocol required.

## 7. Revised build order

1. ✅ Protocol v2 in `espnow_types.h`: `nodeType`, variable payload, `PKT_CMD` /
   `PKT_EVENT` / `PKT_EVENT_ACK`, protocol version byte. Done 2026-09-12,
   `main.cpp` adapted to match, builds clean (0 warnings).
2. ✅ Master: event fast-path handling + per-meter alarm state machine (§4).
   Done 2026-09-12, builds clean.
3. ✅ `ESP_NOW_Slave` (sibling repo, gerrieuae/ESP_NOW_Slave) updated to
   protocol v2 2026-09-12: `nodeType`/`protocolVersion` header fields,
   generic payload via `packSensorData()`, full `pkt_type_t`/`node_type_t`
   mirrored from the master. Declares itself `NODE_TYPE_DISCRETE_INPUT` (its
   placeholder payload exposes a digitalInputs bitmask) — revisit once a
   dedicated relay/water-meter/borehole firmware supersedes this generic
   node. Builds clean, 0 warnings. `PKT_CMD`/`PKT_EVENT` are defined but not
   yet handled by this firmware (still only responds to DISCOVER/POLL).
4. Port W6100L + NetworkManager from AlcoEtherServer into ESP_NOW's master;
   extend the WS server to be reachable over Ethernet; add an MQTT client on
   the master (publisher to start; subscriber for HA→relay commands later).
5. ✅ Peer-identity addressing fix in §5a (key by `senderId`, not radio MAC).
   Done 2026-09-12, builds clean.
6. ✅ Repeater firmware built. Done 2026-09-12 as a new sibling PlatformIO
   project: `~/Documents/PlatformIO/Projects/ESP_NOW_Repeater` (same board,
   `esp32doit-devkit-v1`, reuses the shared `gprintf` lib and the project's
   `CLAUDE.md`). It's a pure MAC-layer store-and-forward proxy — does not
   include/depend on `espnow_types.h` at all (relays raw frames without
   parsing, so it's immune to protocol version changes). Builds clean, 0
   warnings. `NODE_TYPE_REPEATER` in the master's `espnow_types.h` is not
   needed by the repeater itself; it exists so the *master* can eventually
   recognize a repeater if one ever reports into the peer table directly
   (not currently required — the repeater is transparent to the master).
   **Still needs, before it's usable:** the two placeholder MAC addresses
   (`REMOTE_MAC`, `MASTER_MAC`) in its `src/main.cpp` must be edited to the
   real addresses once both the master and camera boards exist and have
   printed their MACs at boot; until then it logs a reminder and refuses to
   start ESP-NOW (fails safe rather than silently registering a useless
   zero-MAC peer). Not committed to git yet (no repo initialized there).
7. Fork jomjol/AI-on-the-edge-device (§5): swap its WiFi/MQTT publish for an
   ESP-NOW send of the final reading; keep its local AP calibration webpage.
8. Node-RED/HA integration: WS client for local dashboard + MQTT for the
   master's outward telemetry/events/camera readings; entity mapping.

## 8. Open items / things to confirm later

- Exact W6100 wiring (reset/CS pins) on the master board — copy from
  AlcoEtherServer's `project_config.h` / board wiring once confirmed same board
  family (`esp32doit-devkit-v1` in both projects, per `platformio.ini`).
- Whether the master's WS server needs auth (AlcoEtherServer has
  `data/logout.html` suggesting some auth pattern already exists there — worth
  reusing rather than inventing new auth for ESP_NOW's dashboard).
- MQTT broker location (Home Assistant's built-in Mosquitto add-on, presumably)
  and topic naming convention for master-published data.
- Peer-identity addressing fix (§5a) — needs to land before repeater/camera
  work starts.
- Where exactly in jomjol's codebase to hook the ESP-NOW send (which source
  file finalizes a reading) — needs a read-through of that codebase once we
  get to step 7.
