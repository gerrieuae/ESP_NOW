/**
 * @file espnow_types.h
 * @brief Shared ESP-NOW protocol types used by both the master and web server.
 *
 * All over-the-air packet definitions and the peer table entry layout live
 * here so that main.cpp and web_server.cpp can both include them without
 * duplicating or forward-declaring anything.
 *
 * Protocol v2: the header now carries a protocol version and the sender's
 * node type, and the fixed sensor_data_t body has been replaced with a
 * generic small payload so that different slave node types (relay,
 * discrete input, water meter, borehole control, ...) can carry different
 * data without changing the wire struct. PKT_CMD and PKT_EVENT round out
 * the type space for master-to-slave commands and unsolicited slave alarms;
 * their payload layouts are defined per node type as those slaves are built.
 *
 * The slave firmware carries its own copy of these definitions. Any change
 * to a packet structure MUST be reflected in the slave project too.
 */

#pragma once

#include <assert.h>
#include <stdint.h>

// ============================================================================
// Sizing constants
// ============================================================================

/** @brief Length of a MAC address in bytes. */
#define MAC_ADDR_LEN        6U

/** @brief Maximum number of slave peers tracked simultaneously. */
#define MAX_PEERS           10U

/** @brief Maximum bytes carried in espnow_packet_t.payload.
 *
 * Sized generously for small telemetry/command/event payloads while keeping
 * every frame far under the ~250-byte ESP-NOW frame limit. Bulk data (e.g.
 * camera images) is deliberately out of scope for this channel.
 */
#define MAX_PAYLOAD_LEN     32U

/** @brief Current wire protocol version, carried in every packet header. */
#define PROTOCOL_VERSION    2U

// ============================================================================
// Node types
// ============================================================================

/**
 * @brief Identifies a node's role on the ESP-NOW mesh.
 *
 * Carried in pkt_header_t.nodeType so the master knows how to interpret a
 * given peer's payloads and which PKT_CMD opcodes are valid for it.
 */
typedef enum {
    NODE_TYPE_UNKNOWN        = 0x00U, /**< Not yet identified.                  */
    NODE_TYPE_MASTER         = 0x01U, /**< The ESP-NOW master itself.           */
    NODE_TYPE_RELAY          = 0x02U, /**< Controls one or more relays.         */
    NODE_TYPE_DISCRETE_INPUT = 0x03U, /**< Reads digital inputs (contacts).     */
    NODE_TYPE_WATER_METER    = 0x04U, /**< Water meter pulse counter.           */
    NODE_TYPE_BOREHOLE_CTRL  = 0x05U, /**< Borehole pump control + protection.  */
    NODE_TYPE_GATEWAY        = 0x06U, /**< Bridge node (if used, see project docs). */
} node_type_t;

// ============================================================================
// Protocol packet types
// ============================================================================

/**
 * @brief Packet type field values carried in pkt_header_t.pktType.
 */
typedef enum {
    PKT_DISCOVER      = 0x01U, /**< Master → broadcast: find all slaves.       */
    PKT_DISCOVER_RESP = 0x02U, /**< Slave  → master:    identify self.         */
    PKT_POLL          = 0x03U, /**< Master → slave:     request sensor data.   */
    PKT_POLL_RESP     = 0x04U, /**< Slave  → master:    current sensor data.   */
    PKT_CMD           = 0x05U, /**< Master → slave:     generic command.       */
    PKT_CMD_ACK       = 0x06U, /**< Slave  → master:    command acknowledged.  */
    PKT_EVENT         = 0x07U, /**< Slave  → master:    unsolicited alarm.     */
    PKT_EVENT_ACK     = 0x08U, /**< Master → slave:     alarm acknowledged.    */
} pkt_type_t;

// ============================================================================
// Payload structures
// ============================================================================

/**
 * @brief Placeholder sensor payload.
 *
 * Replace the fields below with the actual home-monitoring signals once the
 * hardware is finalised (water pulse count, kWh, contact state, etc.).
 * Any change here must be mirrored in the slave firmware. Carried inside
 * espnow_packet_t.payload for PKT_DISCOVER_RESP and PKT_POLL_RESP.
 */
typedef struct {
    uint8_t  nodeId;        /**< Slave node identifier (derived from MAC).    */
    uint16_t analogValue;   /**< Placeholder: raw 12-bit ADC reading.         */
    uint8_t  digitalInputs; /**< Placeholder: bitmask of up to 8 DI channels. */
    uint32_t uptimeSec;     /**< Slave uptime in seconds since last boot.     */
} sensor_data_t;

/**
 * @brief Common header prepended to every over-the-air packet.
 */
typedef struct {
    uint8_t  pktType;         /**< One of pkt_type_t.                          */
    uint8_t  protocolVersion; /**< Must equal PROTOCOL_VERSION.                */
    uint8_t  nodeType;        /**< Sender's role, one of node_type_t.          */
    uint8_t  senderId;        /**< Sender's node ID.                           */
    uint32_t timestampMs;     /**< Sender's millis() value at transmit time.   */
} pkt_header_t;

/**
 * @brief Complete over-the-air packet: header plus a generic small payload.
 *
 * payload is always transmitted at its full MAX_PAYLOAD_LEN size (unused
 * trailing bytes zeroed); payloadLen records how many bytes are meaningful.
 * Interpretation of payload depends on header.pktType and header.nodeType.
 */
typedef struct {
    pkt_header_t header;                    /**< Protocol routing information. */
    uint8_t      payloadLen;                /**< Meaningful bytes in payload.  */
    uint8_t      payload[MAX_PAYLOAD_LEN];  /**< Type-specific payload bytes.  */
} espnow_packet_t;

/** @brief Every espnow_packet_t must fit well within the ESP-NOW frame limit. */
static_assert(sizeof(espnow_packet_t) <= 250U,
              "espnow_packet_t exceeds the ESP-NOW maximum frame size");

/**
 * @brief One entry in the master peer-discovery table.
 *
 * Identity is nodeId (matches pkt_header_t.senderId), not macAddr. A peer
 * reached through a repeater arrives at the master with the repeater's MAC
 * as the ESP-NOW radio source, so macAddr records the "route" address to
 * transmit to (the peer's own MAC when direct, a repeater's MAC otherwise)
 * rather than the peer's own identity.
 */
typedef struct {
    uint8_t       macAddr[MAC_ADDR_LEN]; /**< Route MAC to reach this peer.   */
    uint8_t       nodeId;                /**< Identity: sender's node ID.     */
    uint8_t       nodeType;              /**< Peer's role, one of node_type_t.*/
    uint32_t      lastSeenMs;            /**< millis() of last received frame.*/
    sensor_data_t lastData;              /**< Most recent sensor snapshot.    */
    uint8_t       isActive;              /**< 1 = active, 0 = timed out.      */
} peer_entry_t;

//=============================================================================
