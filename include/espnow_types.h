/**
 * @file espnow_types.h
 * @brief Shared ESP-NOW protocol types used by both the master and web server.
 *
 * All over-the-air packet definitions and the peer table entry layout live
 * here so that main.cpp and web_server.cpp can both include them without
 * duplicating or forward-declaring anything.
 *
 * The slave firmware carries its own copy of these definitions.  Any change
 * to a packet structure MUST be reflected in the slave project too.
 */

#pragma once

#include <stdint.h>

// ============================================================================
// Sizing constants
// ============================================================================

/** @brief Length of a MAC address in bytes. */
#define MAC_ADDR_LEN   6U

/** @brief Maximum number of slave peers tracked simultaneously. */
#define MAX_PEERS      10U

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
} pkt_type_t;

// ============================================================================
// Payload structures
// ============================================================================

/**
 * @brief Placeholder sensor payload.
 *
 * Replace the fields below with the actual home-monitoring signals once the
 * hardware is finalised (water pulse count, kWh, contact state, etc.).
 * Any change here must be mirrored in the slave firmware.
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
    uint8_t  pktType;      /**< One of pkt_type_t.                            */
    uint8_t  senderId;     /**< Sender's node ID.                             */
    uint32_t timestampMs;  /**< Sender's millis() value at transmit time.     */
} pkt_header_t;

/**
 * @brief Complete over-the-air packet: header plus sensor payload.
 */
typedef struct {
    pkt_header_t  header; /**< Protocol routing information.                  */
    sensor_data_t data;   /**< Sensor payload.                               */
} espnow_packet_t;

/**
 * @brief One entry in the master peer-discovery table.
 */
typedef struct {
    uint8_t       macAddr[MAC_ADDR_LEN]; /**< Peer's MAC address (6 bytes).   */
    uint8_t       nodeId;                /**< Peer's self-reported node ID.   */
    uint32_t      lastSeenMs;            /**< millis() of last received frame.*/
    sensor_data_t lastData;              /**< Most recent sensor snapshot.    */
    uint8_t       isActive;              /**< 1 = active, 0 = timed out.      */
} peer_entry_t;

//=============================================================================
