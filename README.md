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
- **LED indicator** – no-slave heartbeat (50 ms blink every 1 s); N active slaves → N × 250 ms pulses separated by 100 ms gaps, 1 s quiet period between cycles

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

## Protocol

All packets share the same 13-byte `espnow_packet_t` wire format (header + sensor payload). Packet types must stay in sync with the slave firmware.

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| `PKT_DISCOVER` | `0x01` | Master → broadcast | Invite all slaves to identify themselves |
| `PKT_DISCOVER_RESP` | `0x02` | Slave → master | Slave identification + initial sensor data |
| `PKT_POLL` | `0x03` | Master → slave | Request latest sensor data (unicast) |
| `PKT_POLL_RESP` | `0x04` | Slave → master | Current sensor data (unicast) |

> **Sensor payload** (`sensor_data_t`) is a placeholder. Replace `analogValue`, `digitalInputs`, and `uptimeSec` fields in `espnow_types.h` to match the actual hardware — any change must be mirrored in the slave project.

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
└── platformio.ini
```
