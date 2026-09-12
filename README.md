# ESP-NOW Master Node

ESP32-based master transceiver for a home-monitoring sensor network. Discovers slave nodes over ESP-NOW, polls them for sensor data every second, and serves a live web dashboard over HTTP/WebSocket.

## Hardware

| Item | Value |
|------|-------|
| Board | ESP32 DOIT DevKit v1 |
| Framework | Arduino (PlatformIO) |
| LED | Onboard (GPIO 2) |

## Features

- **Auto-discovery** – broadcasts `PKT_DISCOVER` every 10 s; newly booted slaves respond automatically
- **Periodic polling** – unicast `PKT_POLL` to each known slave every 1 s, 200 ms per-peer timeout
- **Web dashboard** – single-page app served at `192.168.4.1` (AP) or DHCP address (STA); live peer table pushed via WebSocket every 2 s
- **Home WiFi** – optional STA connection; on boot, shows the 5 strongest networks on the serial monitor for selection (2 s timeout, then auto-connects to `WIFI_DEFAULT_SSID`)
- **Peer management** – tracks up to 10 slaves; marks a peer inactive after 60 s with no response
- **LED indicator** – 1 s blink while no slaves are active; 250 ms blink once at least one slave peer is known

## Getting Started

1. Open the project in PlatformIO.
2. Set `WIFI_DEFAULT_SSID` and `WIFI_PASSWORD` in `src/main.cpp` to match your home network (or leave as-is to skip STA).
3. Build and flash: `pio run -t upload`.
4. Open the serial monitor at 115 200 baud to watch discovery and poll logs.
5. Connect a browser to `http://192.168.4.1` (join the `ESP32-HomeMon` Wi-Fi first, password `homemonitor`) to see the live dashboard.

## Dependencies

```
ESPAsyncWebServer  https://github.com/me-no-dev/ESPAsyncWebServer.git
AsyncTCP           https://github.com/me-no-dev/AsyncTCP.git
```

Installed automatically by PlatformIO via `platformio.ini`.

## Protocol (v2)

Every packet is an `espnow_packet_t`: a fixed header followed by a small
generic payload (`payloadLen` + up to `MAX_PAYLOAD_LEN` = 32 bytes), well
under the ~250-byte ESP-NOW frame limit (enforced by a compile-time
`static_assert`). Payload contents are interpreted per `pktType`/`nodeType`.
Packet types must stay in sync with any slave/repeater firmware.

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| `PKT_DISCOVER` | `0x01` | Master → broadcast | Invite all slaves to identify themselves |
| `PKT_DISCOVER_RESP` | `0x02` | Slave → master | Slave identification + initial sensor data |
| `PKT_POLL` | `0x03` | Master → slave | Request latest sensor data (unicast) |
| `PKT_POLL_RESP` | `0x04` | Slave → master | Current sensor data (unicast) |
| `PKT_CMD` | `0x05` | Master → slave | Generic command (opcode/params in payload) — not yet used by any slave |
| `PKT_CMD_ACK` | `0x06` | Slave → master | Command acknowledged — not yet used |
| `PKT_EVENT` | `0x07` | Slave → master | Unsolicited event, handled immediately (not tied to the poll cycle) |
| `PKT_EVENT_ACK` | `0x08` | Master → slave | Alarm acknowledged — not yet used |

### Events and the "hasn't stopped" watchdog

`PKT_EVENT` currently carries one event type, `EVENT_METER_READING`
(`event_payload_t{eventType, value}` — a cumulative reading, e.g. total
litres or kWh). For each reporting peer the master tracks whether
consecutive readings keep changing (`updateValueWatchdog()` in `main.cpp`):
if the value changes continuously for `WATER_NO_STOP_TIMEOUT_MS` (30 min
default) without ever holding steady, an alarm is raised — e.g. a tap left
running or a stuck valve. A reading that repeats the previous value means
activity stopped, clearing the alarm. This is source-agnostic: it works the
same whether the reading came directly over ESP-NOW or via a repeater. On
raise/clear the master immediately broadcasts the updated peer table over
the WS dashboard (`"alarm"` field per peer) rather than waiting for the next
periodic heartbeat.

The header carries `protocolVersion` (currently `2`) and `nodeType` — see
`node_type_t` in `espnow_types.h` for the current node taxonomy (`RELAY`,
`DISCRETE_INPUT`, `WATER_METER`, `BOREHOLE_CTRL`, `GATEWAY`, plus `MASTER`).

**Peer identity is `nodeId` (`header.senderId`), not MAC address.**
`peer_entry_t.macAddr` is the *route* to a peer — the peer's own MAC when
directly reachable, or a repeater's MAC when it isn't — and is refreshed on
every packet received from that peer. This is what lets a peer be relayed
through an [ESP_NOW_Repeater](https://github.com/gerrieuae/ESP_NOW_Repeater)
node transparently, with no special-case code in the master.

> **Sensor payload** (`sensor_data_t`) is still a placeholder, packed/unpacked
> into the generic payload buffer via `unpackSensorData()`. Replace
> `analogValue`, `digitalInputs`, and `uptimeSec` fields in `espnow_types.h`
> to match the actual hardware — any change must be mirrored in any slave
> firmware that talks to this master.

[ESP_NOW_Slave](https://github.com/gerrieuae/ESP_NOW_Slave) has been updated
to match this protocol v2 wire format.

See [`docs/plan.md`](docs/plan.md) for the full mesh expansion plan (camera
meter reading, repeater, Ethernet/MQTT gateway, Home Assistant integration).

## Key Configuration

| Constant | Default | Description |
|----------|---------|-------------|
| `AP_SSID` | `"ESP32-HomeMon"` | Soft-AP SSID (slaves scan for this) |
| `AP_PASSWORD` | `"homemonitor"` | Soft-AP password |
| `AP_CHANNEL` | `1` | WiFi channel for AP and ESP-NOW |
| `DISCOVER_INTERVAL_MS` | `10 000` | Discovery broadcast period (ms) |
| `POLL_INTERVAL_MS` | `1 000` | Poll cycle period (ms) |
| `POLL_PEER_TIMEOUT_MS` | `200` | Per-slave response timeout (ms) |
| `PEER_STALE_MS` | `60 000` | Time before a silent peer is marked inactive (ms) |
| `MAX_PEERS` | `10` | Maximum simultaneously tracked slaves |
| `LED_SCAN_BLINK_MS` | `1 000` | LED toggle period while no slaves are active (ms) |
| `LED_CONN_BLINK_MS` | `250` | LED toggle period when at least one slave is active (ms) |
| `WIFI_DEFAULT_SSID` | `"CreatronE"` | Auto-connect target if user makes no serial choice |

## Project Structure

```
ESP_NOW/
├── src/
│   ├── main.cpp          # Master state machine, ESP-NOW logic, LED
│   └── web_server.cpp    # HTTP + WebSocket server implementation
├── include/
│   ├── espnow_types.h    # Shared packet and peer-table type definitions
│   ├── web_server.h      # Web server API
│   ├── hardware.h        # Board pin / peripheral definitions
│   └── gprintf/          # Debug UART printf library
├── docs/
│   └── plan.md           # Mesh expansion plan (camera, repeater, gateway, HA)
└── platformio.ini
```

## Related

- **Slave firmware**: [ESP_NOW_Slave](https://github.com/gerrieuae/ESP_NOW_Slave) — sensor node responding to discovery/poll
- **Repeater firmware**: [ESP_NOW_Repeater](https://github.com/gerrieuae/ESP_NOW_Repeater) — fixed one-hop relay for slaves out of direct radio range
