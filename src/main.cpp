/**
 * @file main.cpp
 * @brief ESP-NOW Master Transceiver – peer discovery, polling, and web dashboard.
 *
 * On startup the master creates a WiFi soft-AP ("ESP32-HomeMon") so that a
 * browser can connect and view the live dashboard at 192.168.4.1.
 * It simultaneously operates ESP-NOW in AP_STA mode to broadcast
 * PKT_DISCOVER frames and poll responding slave nodes every second.
 *
 * Sensor data structure is a placeholder – replace sensor_data_t fields to
 * match the actual home-monitoring signals (water, electricity, etc.).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_log.h>
#include <stdint.h>
#include <string.h>
#include "gprintf.h"
#include "espnow_types.h"
#include "web_server.h"

// ============================================================================
// Constants and limits
// ============================================================================

/** @brief This node's identifier (master is always 1). */
#define MASTER_NODE_ID          1U

/** @brief Depth of the inter-task receive queue (callback → loop). */
#define RX_QUEUE_DEPTH          8U

/** @brief WiFi soft-AP network name broadcast by the master. */
#define AP_SSID                 "ESP32-HomeMon"

/** @brief WiFi soft-AP password (min 8 chars for WPA2). */
#define AP_PASSWORD             "homemonitor"

/** @brief WiFi channel shared by the soft-AP and ESP-NOW. */
#define AP_CHANNEL              1U

/** @brief How often the peer table is pushed to WebSocket clients (ms). */
#define WEB_BROADCAST_INTERVAL_MS 2000U

/** @brief How often to re-broadcast a discovery frame (ms). */
#define DISCOVER_INTERVAL_MS    10000U

/** @brief How long to stay in the discovery-wait window (ms). */
#define DISCOVER_TIMEOUT_MS      2000U

/** @brief How often to start a full poll cycle across all known peers (ms). */
#define POLL_INTERVAL_MS         1000U

/** @brief Per-peer response timeout during a poll cycle (ms). */
#define POLL_PEER_TIMEOUT_MS      200U

/** @brief Peer is marked inactive after this many ms without a response. */
#define PEER_STALE_MS           60000U

/** @brief Retry delay before re-attempting initialisation after an error (ms). */
#define INIT_RETRY_DELAY_MS      5000U

/** @brief Minimum required payload length for a valid received packet. */
#define MIN_PACKET_LEN          ((int32_t)sizeof(espnow_packet_t))

/** @brief Password used when connecting to the user's home WiFi network. */
#define WIFI_PASSWORD           "L1nux112358"

/** @brief Maximum number of scan results shown for user selection. */
#define WIFI_MAX_SCAN_RESULTS   5U

/** @brief Maximum SSID length including null terminator. */
#define WIFI_SSID_MAX_LEN       33U

/** @brief Timeout waiting for an async WiFi scan to complete (ms). */
#define WIFI_SCAN_TIMEOUT_MS    10000U

/** @brief Default SSID to connect to when no user input is received. */
#define WIFI_DEFAULT_SSID       "CreatronE"

/** @brief Time the user has to enter a network choice before auto-connecting (ms). */
#define WIFI_SELECT_TIMEOUT_MS  2000U

/** @brief Timeout waiting for a DHCP IP assignment after WiFi.begin() (ms). */
#define WIFI_CONNECT_TIMEOUT_MS 15000U

/** @brief Delay after WiFi.mode(WIFI_STA) before starting the scan (ms).
 *  The radio stack is asynchronously initialised; without this pause
 *  WiFi.scanNetworks() returns WIFI_SCAN_FAILED instantly. */
#define WIFI_RADIO_SETTLE_MS    300U

/** @brief LED blink interval while no slave peers are active (ms). */
#define LED_SCAN_BLINK_MS       1000U

/** @brief LED blink interval when at least one slave peer is active (ms). */
#define LED_CONN_BLINK_MS        250U

// ============================================================================
// Local type definitions (internal to this file)
// ============================================================================

/**
 * @brief Item stored in the ring buffer between the ESP-NOW callback and loop().
 */
typedef struct {
    uint8_t         srcMac[MAC_ADDR_LEN]; /**< Source MAC address.              */
    espnow_packet_t pkt;                  /**< Full received packet.            */
} rx_item_t;

/**
 * @brief Master state-machine states.
 */
typedef enum {
    MASTER_STATE_WIFI_SCAN,         /**< Set STA mode; start radio-settle timer.   */
    MASTER_STATE_WIFI_RADIO_WAIT,   /**< Wait for radio stack to be ready.         */
    MASTER_STATE_WIFI_SCAN_WAIT,    /**< Wait for async scan to complete.          */
    MASTER_STATE_WIFI_SELECT,       /**< Display top 5; wait for user key press.   */
    MASTER_STATE_WIFI_CONNECT,      /**< Connect to user-selected network.         */
    MASTER_STATE_WIFI_CONNECT_WAIT, /**< Wait for DHCP IP assignment.              */
    MASTER_STATE_INIT,              /**< Initialise WiFi AP and ESP-NOW.           */
    MASTER_STATE_DISCOVER,       /**< Broadcast a PKT_DISCOVER frame.           */
    MASTER_STATE_DISCOVER_WAIT,  /**< Wait for PKT_DISCOVER_RESP replies.       */
    MASTER_STATE_POLL_NEXT,      /**< Select the next active peer to poll.      */
    MASTER_STATE_POLL_SEND,      /**< Transmit PKT_POLL to selected peer.       */
    MASTER_STATE_POLL_WAIT,      /**< Wait for PKT_POLL_RESP or timeout.        */
    MASTER_STATE_IDLE,           /**< Wait until the next discover/poll cycle.  */
    MASTER_STATE_ERROR,          /**< Unrecoverable error – retry after delay.  */
} master_state_t;

// ============================================================================
// Static state
// ============================================================================

/** @brief Discovered peer table. */
static peer_entry_t    peerTable[MAX_PEERS];

/** @brief Number of entries currently in peerTable (active + inactive). */
static uint8_t         peerCount        = 0U;

/** @brief Current state of the master state machine. */
static master_state_t  masterState      = MASTER_STATE_WIFI_SCAN;

/** @brief Timestamp recorded when the current state was entered (ms). */
static uint32_t        stateTimestampMs = 0U;

/** @brief Timestamp of the last discovery broadcast (ms). */
static uint32_t        discoverTimerMs  = 0U;

/** @brief Timestamp of the last poll cycle start (ms). */
static uint32_t        pollTimerMs      = 0U;

/** @brief Index into peerTable of the peer currently being polled. */
static uint8_t         pollPeerIndex    = 0U;

/** @brief Timestamp of the last WebSocket broadcast (ms). */
static uint32_t        webBroadcastMs   = 0U;

/** @brief SSID of the user-selected network ('\0' if none chosen). */
static char            selectedSsid[WIFI_SSID_MAX_LEN];

/** @brief Number of entries stored in scanSsidList / scanRssiList. */
static uint8_t         scanResultCount;

/** @brief SSID strings for the top scanned networks (strongest first). */
static char            scanSsidList[WIFI_MAX_SCAN_RESULTS][WIFI_SSID_MAX_LEN];

/** @brief RSSI values (dBm, negative) for the top scanned networks. */
static int32_t         scanRssiList[WIFI_MAX_SCAN_RESULTS];

/** @brief True once the network selection menu has been displayed. */
static bool            scanMenuDisplayed;

/** @brief STA IP captured at the moment WL_CONNECTED is confirmed.
 *  Empty string when no STA connection was established. */
static char            confirmedStaIp[16];

/** @brief Broadcast MAC address – reaches every node on the channel. */
static const uint8_t   BROADCAST_MAC[MAC_ADDR_LEN] = {
    0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU
};

// ---------------------------------------------------------------------------
// Receive queue – populated by the ESP-NOW WiFi-task callback, drained by
// loop().  Volatile because it is written from a different RTOS task.
// ---------------------------------------------------------------------------

/** @brief Ring-buffer holding packets pending processing. */
static volatile rx_item_t rxQueue[RX_QUEUE_DEPTH];

/** @brief Write index for rxQueue (updated by the ESP-NOW callback). */
static volatile uint8_t   rxHead = 0U;

/** @brief Read index for rxQueue (updated by the main loop). */
static volatile uint8_t   rxTail = 0U;

// ---------------------------------------------------------------------------
// LED state
// ---------------------------------------------------------------------------

/** @brief millis() of the last onboard LED toggle. */
static uint32_t    ledToggleMs = 0U;

// ============================================================================
// Forward declarations
// ============================================================================

static void     updateLed           (void);
static void     masterProcess       (void);
static void     processRxQueue      (void);
static void     handleDiscoverResp  (const uint8_t *srcMac, const espnow_packet_t *pkt);
static void     handlePollResp      (const uint8_t *srcMac, const espnow_packet_t *pkt);
static uint8_t  findPeer            (const uint8_t *macAddr);
static uint8_t  addOrUpdatePeer     (const uint8_t *macAddr, uint8_t nodeId);
static void     registerEspNowPeer  (const uint8_t *macAddr);
static void     evictStalePeers     (void);
static void     sendPacket          (const uint8_t *destMac, pkt_type_t pktType);
static uint8_t  countActivePeers    (void);
static uint8_t  findNextActivePeer  (uint8_t startIdx);
static bool     initEspNowMaster    (void);
static void     printPeerTable      (void);
static void     macToString         (const uint8_t *mac, char *buf);
static void     storeScanResults    (uint8_t totalFound);
static void     displayScanMenu     (void);
static bool     readSerialChoice    (uint8_t *outChoice);

// ============================================================================
// ESP-NOW callbacks  (execute in the WiFi task – not the Arduino loop task)
// ============================================================================

/**
 * @brief Invoked by ESP-NOW after each attempted transmission.
 * @param[in] macAddr   Destination MAC of the transmission.
 * @param[in] status    ESP_NOW_SEND_SUCCESS or ESP_NOW_SEND_FAIL.
 */
static void onDataSent(const uint8_t *macAddr, esp_now_send_status_t status)
{
    (void)macAddr;
    if (status != ESP_NOW_SEND_SUCCESS)
    {
        gprintf(gDBG, "[MASTER] TX delivery failed\r\n");
    }
}

/**
 * @brief Invoked by ESP-NOW when a packet is received.
 *
 * Runs in the WiFi task context.  Copies the frame into rxQueue so that the
 * main loop can process it without blocking the WiFi stack.
 *
 * @param[in] srcMac    6-byte source MAC address of the sender.
 * @param[in] data      Raw payload bytes.
 * @param[in] dataLen   Payload length in bytes.  Type int is mandated by the
 *                      esp_now_recv_cb_t API signature.
 */
static void onDataRecv(const uint8_t *srcMac,
                       const uint8_t *data,
                       int            dataLen) /* int: required by esp_now_recv_cb_t */
{
    if ((srcMac == NULL) || (data == NULL))
        return;
    if ((int32_t)dataLen < MIN_PACKET_LEN)
        return;

    uint8_t nextHead = (rxHead + 1U) % RX_QUEUE_DEPTH;
    if (nextHead == rxTail)
        return; /* Queue full – silently drop; main loop is too slow. */

    memcpy((void *)rxQueue[rxHead].srcMac,
           srcMac,
           MAC_ADDR_LEN);
    memcpy((void *)&rxQueue[rxHead].pkt,
           data,
           sizeof(espnow_packet_t));
    rxHead = nextHead;
}

// ============================================================================
// Peer table management
// ============================================================================

/**
 * @brief Searches peerTable for an entry with a matching MAC address.
 * @param[in] macAddr   6-byte MAC address to find.
 * @return              Index in peerTable on success, MAX_PEERS if not found.
 */
static uint8_t findPeer(const uint8_t *macAddr)
{
    for (uint8_t i = 0U; i < peerCount; i++)
    {
        if (memcmp(peerTable[i].macAddr, macAddr, MAC_ADDR_LEN) == 0)
            return i;
    }
    return MAX_PEERS;
}

/**
 * @brief Adds a new peer to peerTable, or returns the index of an existing one.
 *
 * If the MAC is already in the table the existing entry is reactivated and its
 * nodeId is updated.  If the table is full MAX_PEERS is returned and the peer
 * is not added.
 *
 * @param[in] macAddr   Peer's 6-byte MAC address.
 * @param[in] nodeId    Peer's self-reported node identifier.
 * @return              Index in peerTable, or MAX_PEERS if the table is full.
 */
static uint8_t addOrUpdatePeer(const uint8_t *macAddr, uint8_t nodeId)
{
    uint8_t existingIdx = findPeer(macAddr);
    if (existingIdx < MAX_PEERS)
    {
        peerTable[existingIdx].nodeId    = nodeId;
        peerTable[existingIdx].isActive  = 1U;
        peerTable[existingIdx].lastSeenMs = millis();
        return existingIdx;
    }

    if (peerCount >= MAX_PEERS)
    {
        gprintf(gDBG, "[MASTER] Peer table full – cannot add new peer\r\n");
        return MAX_PEERS;
    }

    uint8_t newIdx = peerCount;
    memcpy(peerTable[newIdx].macAddr, macAddr, MAC_ADDR_LEN);
    peerTable[newIdx].nodeId      = nodeId;
    peerTable[newIdx].lastSeenMs  = millis();
    peerTable[newIdx].isActive    = 1U;
    memset(&peerTable[newIdx].lastData, 0, sizeof(sensor_data_t));
    peerCount++;

    registerEspNowPeer(macAddr);

    char macStr[18];
    macToString(macAddr, macStr);
    gprintf(gDBG, "[MASTER] New peer: ID=%u  MAC=%s  (total: %u)\r\n",
            (uint32_t)nodeId, macStr, (uint32_t)peerCount);

    return newIdx;
}

/**
 * @brief Registers a MAC address with the ESP-NOW driver as a unicast peer.
 * @param[in] macAddr   6-byte MAC address to register.
 */
static void registerEspNowPeer(const uint8_t *macAddr)
{
    if (esp_now_is_peer_exist(macAddr))
        return;

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, macAddr, MAC_ADDR_LEN);
    peerInfo.channel = 0U;
    peerInfo.encrypt = false;

    esp_err_t result = esp_now_add_peer(&peerInfo);
    if (result != ESP_OK)
    {
        char macStr[18];
        macToString(macAddr, macStr);
        gprintf(gDBG, "[MASTER] esp_now_add_peer(%s) err=%d\r\n",
                macStr, (int32_t)result);
    }
}

/**
 * @brief Marks peers inactive when no frame has been received within PEER_STALE_MS.
 */
static void evictStalePeers(void)
{
    uint32_t nowMs = millis();
    for (uint8_t i = 0U; i < peerCount; i++)
    {
        if (peerTable[i].isActive &&
            ((nowMs - peerTable[i].lastSeenMs) > PEER_STALE_MS))
        {
            char macStr[18];
            macToString(peerTable[i].macAddr, macStr);
            gprintf(gDBG, "[MASTER] Peer %u (%s) timed out\r\n",
                    (uint32_t)peerTable[i].nodeId, macStr);
            peerTable[i].isActive = 0U;
        }
    }
}

// ============================================================================
// Packet send / receive handling
// ============================================================================

/**
 * @brief Constructs and sends a typed ESP-NOW packet to the given destination.
 *
 * The data fields are zeroed; this is appropriate for PKT_DISCOVER and
 * PKT_POLL where no sensor payload is carried by the master.
 *
 * @param[in] destMac   6-byte destination MAC address.
 * @param[in] pktType   Protocol packet type (pkt_type_t).
 */
static void sendPacket(const uint8_t *destMac, pkt_type_t pktType)
{
    espnow_packet_t outPkt;
    memset(&outPkt, 0, sizeof(outPkt));
    outPkt.header.pktType     = (uint8_t)pktType;
    outPkt.header.senderId    = MASTER_NODE_ID;
    outPkt.header.timestampMs = millis();

    esp_err_t result = esp_now_send(destMac,
                                    (const uint8_t *)&outPkt,
                                    sizeof(outPkt));
    if (result != ESP_OK)
    {
        gprintf(gDBG, "[MASTER] esp_now_send err=%d\r\n", (int32_t)result);
    }
}

/**
 * @brief Handles a PKT_DISCOVER_RESP received from a slave.
 *
 * Adds the slave to the peer table (or updates it if already known) and
 * stores the initial sensor snapshot that arrived with the response.
 *
 * @param[in] srcMac    6-byte source MAC of the responding slave.
 * @param[in] pkt       Pointer to the fully received packet.
 */
static void handleDiscoverResp(const uint8_t *srcMac, const espnow_packet_t *pkt)
{
    uint8_t peerIdx = addOrUpdatePeer(srcMac, pkt->data.nodeId);
    if (peerIdx < MAX_PEERS)
    {
        memcpy(&peerTable[peerIdx].lastData, &pkt->data, sizeof(sensor_data_t));
    }
}

/**
 * @brief Handles a PKT_POLL_RESP received from a known slave.
 *
 * Updates the peer's lastSeenMs timestamp and sensor data snapshot.
 * If the source MAC is unknown the frame is treated as an implicit
 * discovery response.
 *
 * @param[in] srcMac    6-byte source MAC of the responding slave.
 * @param[in] pkt       Pointer to the fully received packet.
 */
static void handlePollResp(const uint8_t *srcMac, const espnow_packet_t *pkt)
{
    uint8_t peerIdx = findPeer(srcMac);
    if (peerIdx >= MAX_PEERS)
    {
        handleDiscoverResp(srcMac, pkt);
        return;
    }

    peerTable[peerIdx].lastSeenMs = millis();
    peerTable[peerIdx].isActive   = 1U;
    memcpy(&peerTable[peerIdx].lastData, &pkt->data, sizeof(sensor_data_t));

    gprintf(gDBG,
            "[MASTER] node=%u  analog=%u  din=0x%02X  uptime=%us\r\n",
            (uint32_t)pkt->data.nodeId,
            (uint32_t)pkt->data.analogValue,
            (uint32_t)pkt->data.digitalInputs,
            pkt->data.uptimeSec);
}

/**
 * @brief Drains rxQueue and dispatches each packet to its protocol handler.
 *
 * Must be called from loop() – never from the ESP-NOW callback context.
 */
static void processRxQueue(void)
{
    while (rxTail != rxHead)
    {
        rx_item_t item;
        memcpy(item.srcMac, (const void *)rxQueue[rxTail].srcMac, MAC_ADDR_LEN);
        memcpy(&item.pkt,   (const void *)&rxQueue[rxTail].pkt,   sizeof(espnow_packet_t));
        rxTail = (rxTail + 1U) % RX_QUEUE_DEPTH;

        switch ((pkt_type_t)item.pkt.header.pktType)
        {
        case PKT_DISCOVER_RESP:
            handleDiscoverResp(item.srcMac, &item.pkt);
            break;

        case PKT_POLL_RESP:
            handlePollResp(item.srcMac, &item.pkt);
            break;

        default:
            break;
        }
    }
}

// ============================================================================
// Utility helpers
// ============================================================================

/**
 * @brief Formats six raw MAC bytes into the "XX:XX:XX:XX:XX:XX\0" string.
 * @param[in]  mac  6-byte MAC address.
 * @param[out] buf  Output buffer – caller must provide at least 18 bytes.
 */
static void macToString(const uint8_t *mac, char *buf)
{
    static const char hexChars[] = "0123456789ABCDEF";
    for (uint8_t i = 0U; i < MAC_ADDR_LEN; i++)
    {
        buf[(uint8_t)(i * 3U)]      = hexChars[(mac[i] >> 4U) & 0x0FU];
        buf[(uint8_t)(i * 3U + 1U)] = hexChars[mac[i] & 0x0FU];
        buf[(uint8_t)(i * 3U + 2U)] = (i < (MAC_ADDR_LEN - 1U)) ? ':' : '\0';
    }
    buf[17] = '\0';
}

/**
 * @brief Prints the full peer table to the debug UART.
 */
static void printPeerTable(void)
{
    uint32_t nowMs = millis();
    gprintf(gDBG, "\r\n[MASTER] ===== Peer Table (%u entries) =====\r\n",
            (uint32_t)peerCount);
    for (uint8_t i = 0U; i < peerCount; i++)
    {
        char macStr[18];
        macToString(peerTable[i].macAddr, macStr);
        gprintf(gDBG,
                "  [%u] MAC=%-17s  ID=%u  Active=%u  LastSeen=%ums ago\r\n",
                (uint32_t)i,
                macStr,
                (uint32_t)peerTable[i].nodeId,
                (uint32_t)peerTable[i].isActive,
                nowMs - peerTable[i].lastSeenMs);
    }
    gprintf(gDBG, "[MASTER] ==========================================\r\n\r\n");
}

/**
 * @brief Returns the count of peers currently marked as active.
 * @return Number of active peers (0..MAX_PEERS).
 */
static uint8_t countActivePeers(void)
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < peerCount; i++)
    {
        if (peerTable[i].isActive)
            count++;
    }
    return count;
}

/**
 * @brief Finds the lowest-index active peer at or after startIdx.
 * @param[in] startIdx  First index to check.
 * @return              Peer table index, or MAX_PEERS when no active peer is found.
 */
static uint8_t findNextActivePeer(uint8_t startIdx)
{
    for (uint8_t i = startIdx; i < peerCount; i++)
    {
        if (peerTable[i].isActive)
            return i;
    }
    return MAX_PEERS;
}

// ============================================================================
// Initialisation
// ============================================================================

/**
 * @brief Initialises WiFi in station mode and starts the ESP-NOW stack.
 *
 * Registers both the send and receive callbacks, then adds the broadcast
 * MAC so that PKT_DISCOVER frames can be sent without a registered peer.
 *
 * @return true on success, false if any ESP-NOW step failed.
 */
static bool initEspNowMaster(void)
{
    WiFi.mode(WIFI_AP_STA);

    if (!WiFi.softAP(AP_SSID, AP_PASSWORD, (int32_t)AP_CHANNEL))
    {
        gprintf(gDBG, "[MASTER] WiFi.softAP() failed\r\n");
        return false;
    }

    gprintf(gDBG, "[MASTER] Soft-AP \"%s\" on ch%u, IP=%s\r\n",
            AP_SSID, (uint32_t)AP_CHANNEL,
            WiFi.softAPIP().toString().c_str());

    if (esp_now_init() != ESP_OK)
    {
        gprintf(gDBG, "[MASTER] esp_now_init() failed\r\n");
        return false;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    registerEspNowPeer(BROADCAST_MAC);

    webServerInit(confirmedStaIp);

    uint8_t ownMac[MAC_ADDR_LEN];
    WiFi.macAddress(ownMac);
    char macStr[18];
    macToString(ownMac, macStr);
    gprintf(gDBG, "[MASTER] ESP-NOW ready. MAC=%s\r\n", macStr);
    return true;
}

// ============================================================================
// WiFi scan / selection helpers
// ============================================================================

/**
 * @brief Copies the strongest scan results into scanSsidList / scanRssiList.
 *
 * WiFi.scanNetworks() returns results sorted by descending RSSI, so the first
 * WIFI_MAX_SCAN_RESULTS entries are already the strongest networks.
 *
 * @param[in] totalFound  Total network count returned by WiFi.scanComplete().
 */
static void storeScanResults(uint8_t totalFound)
{
    scanResultCount = (totalFound < (uint8_t)WIFI_MAX_SCAN_RESULTS)
                      ? totalFound
                      : (uint8_t)WIFI_MAX_SCAN_RESULTS;

    for (uint8_t i = 0U; i < scanResultCount; i++)
    {
        /* WiFi.SSID() returns an Arduino String – .c_str() is used to
         * immediately extract the raw pointer into a plain char array. */
        strncpy(scanSsidList[i], WiFi.SSID(i).c_str(), WIFI_SSID_MAX_LEN - 1U);
        scanSsidList[i][WIFI_SSID_MAX_LEN - 1U] = '\0';
        scanRssiList[i] = (int32_t)WiFi.RSSI(i);
    }
}

/**
 * @brief Prints the network selection menu to the debug UART once.
 */
static void displayScanMenu(void)
{
    gprintf(gDBG, "\r\n[WIFI] Top %u networks (strongest first):\r\n",
            (uint32_t)scanResultCount);
    for (uint8_t i = 0U; i < scanResultCount; i++)
    {
        gprintf(gDBG, "  [%u] %-32s  %ddBm\r\n",
                (uint32_t)(i + 1U),
                scanSsidList[i],
                (int32_t)scanRssiList[i]);
    }
    gprintf(gDBG, "\r\nEnter 1-%u to connect (auto-connecting to \"%s\" in %us): ",
            (uint32_t)scanResultCount,
            WIFI_DEFAULT_SSID,
            (uint32_t)(WIFI_SELECT_TIMEOUT_MS / 1000U));
}

/**
 * @brief Non-blocking read of one ASCII digit from the debug serial port.
 *
 * Returns false immediately if no byte is waiting.  Any character outside
 * '1'..'5' sets *outChoice to 0 (treated as "skip WiFi").
 *
 * @param[out] outChoice  Receives the numeric choice (1..5 = valid, 0 = skip).
 * @return                true if a byte was consumed; false if none available.
 */
static bool readSerialChoice(uint8_t *outChoice)
{
    if (!Serial.available())
        return false;

    uint8_t ch = (uint8_t)Serial.read();
    if ((ch >= (uint8_t)'1') && (ch <= (uint8_t)'5'))
    {
        *outChoice = ch - (uint8_t)'0';
    }
    else
    {
        *outChoice = 0U;
    }
    return true;
}

// ============================================================================
// LED status indicator
// ============================================================================

/**
 * @brief Toggles the onboard LED at a rate that reflects slave connection state.
 *
 * Blinks at LED_SCAN_BLINK_MS while no active slave peers are known, and
 * switches to the faster LED_CONN_BLINK_MS cadence once at least one slave
 * is active.
 */
static void updateLed(void)
{
    uint32_t nowMs    = millis();
    uint32_t interval = (countActivePeers() > 0U) ? LED_CONN_BLINK_MS
                                                   : LED_SCAN_BLINK_MS;
    if ((nowMs - ledToggleMs) >= interval)
    {
        ledToggleMs = nowMs;
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
}

// ============================================================================
// Master state machine
// ============================================================================

/**
 * @brief Non-blocking master transceiver state machine.
 *
 * Must be called on every iteration of loop().  Processes the receive queue,
 * evicts stale peers, and advances through the discovery/poll cycle using
 * millis()-based timers without blocking.
 *
 * State flow:
 *   INIT → DISCOVER → DISCOVER_WAIT → IDLE ↔ POLL_NEXT → POLL_SEND
 *                                              ↑                 ↓
 *                                         POLL_WAIT ←───────────┘
 */
static void masterProcess(void)
{
    updateLed();
    webServerProcess();
    processRxQueue();
    evictStalePeers();

    uint32_t nowMs = millis();

    switch (masterState)
    {
    /* ------------------------------------------------------------------ */
    case MASTER_STATE_WIFI_SCAN:
        gprintf(gDBG, "[WIFI] Initialising radio...\r\n");
        WiFi.mode(WIFI_STA);
        stateTimestampMs = nowMs;
        masterState      = MASTER_STATE_WIFI_RADIO_WAIT;
        break;

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_WIFI_RADIO_WAIT:
        if ((nowMs - stateTimestampMs) >= WIFI_RADIO_SETTLE_MS)
        {
            gprintf(gDBG, "[WIFI] Scanning for networks...\r\n");
            WiFi.scanNetworks(/* async= */ true);
            stateTimestampMs = nowMs;
            masterState      = MASTER_STATE_WIFI_SCAN_WAIT;
        }
        break;

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_WIFI_SCAN_WAIT:
    {
        int16_t scanResult = (int16_t)WiFi.scanComplete();
        if (scanResult == (int16_t)WIFI_SCAN_RUNNING)
        {
            if ((nowMs - stateTimestampMs) >= WIFI_SCAN_TIMEOUT_MS)
            {
                gprintf(gDBG, "[WIFI] Scan timed out – skipping WiFi selection\r\n");
                masterState = MASTER_STATE_INIT;
            }
        }
        else if (scanResult <= 0)
        {
            gprintf(gDBG, "[WIFI] No networks found – skipping WiFi selection\r\n");
            WiFi.scanDelete();
            masterState = MASTER_STATE_INIT;
        }
        else
        {
            storeScanResults((uint8_t)scanResult);
            WiFi.scanDelete();
            scanMenuDisplayed = false;
            stateTimestampMs  = nowMs;
            masterState       = MASTER_STATE_WIFI_SELECT;
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_WIFI_SELECT:
    {
        if (!scanMenuDisplayed)
        {
            displayScanMenu();
            scanMenuDisplayed = true;
        }
        uint8_t choice = 0U;
        if (readSerialChoice(&choice))
        {
            if ((choice >= 1U) && (choice <= scanResultCount))
            {
                strncpy(selectedSsid,
                        scanSsidList[choice - 1U],
                        WIFI_SSID_MAX_LEN - 1U);
                selectedSsid[WIFI_SSID_MAX_LEN - 1U] = '\0';
                gprintf(gDBG, "\r\n[WIFI] Selected: \"%s\"\r\n", selectedSsid);
                stateTimestampMs = nowMs;
                masterState      = MASTER_STATE_WIFI_CONNECT;
            }
            else
            {
                gprintf(gDBG, "\r\n[WIFI] Skipping WiFi selection\r\n");
                masterState = MASTER_STATE_INIT;
            }
        }
        else if ((nowMs - stateTimestampMs) >= WIFI_SELECT_TIMEOUT_MS)
        {
            strncpy(selectedSsid, WIFI_DEFAULT_SSID, WIFI_SSID_MAX_LEN - 1U);
            selectedSsid[WIFI_SSID_MAX_LEN - 1U] = '\0';
            gprintf(gDBG, "\r\n[WIFI] Auto-connecting to \"%s\"\r\n", selectedSsid);
            stateTimestampMs = nowMs;
            masterState      = MASTER_STATE_WIFI_CONNECT;
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_WIFI_CONNECT:
        gprintf(gDBG, "[WIFI] Connecting to \"%s\"...\r\n", selectedSsid);
        WiFi.begin((const char *)selectedSsid, WIFI_PASSWORD);
        stateTimestampMs = nowMs;
        masterState      = MASTER_STATE_WIFI_CONNECT_WAIT;
        break;

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_WIFI_CONNECT_WAIT:
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            IPAddress staIp = WiFi.localIP();
            snprintf(confirmedStaIp, sizeof(confirmedStaIp), "%u.%u.%u.%u",
                     staIp[0], staIp[1], staIp[2], staIp[3]);
            gprintf(gDBG, "[WIFI] Connected! IP=%s\r\n", confirmedStaIp);
            masterState = MASTER_STATE_INIT;
        }
        else if ((nowMs - stateTimestampMs) >= WIFI_CONNECT_TIMEOUT_MS)
        {
            gprintf(gDBG, "[WIFI] Connection timed out – continuing without WiFi\r\n");
            confirmedStaIp[0] = '\0';
            selectedSsid[0]   = '\0';
            masterState       = MASTER_STATE_INIT;
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_INIT:
        memset(peerTable, 0, sizeof(peerTable));
        peerCount       = 0U;
        pollPeerIndex   = 0U;
        discoverTimerMs = nowMs;
        pollTimerMs     = nowMs;
        stateTimestampMs = nowMs;

        if (initEspNowMaster())
        {
            masterState = MASTER_STATE_DISCOVER;
        }
        else
        {
            masterState = MASTER_STATE_ERROR;
        }
        break;

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_DISCOVER:
        gprintf(gDBG, "[MASTER] Broadcasting PKT_DISCOVER...\r\n");
        sendPacket(BROADCAST_MAC, PKT_DISCOVER);
        stateTimestampMs = nowMs;
        discoverTimerMs  = nowMs;
        masterState      = MASTER_STATE_DISCOVER_WAIT;
        break;

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_DISCOVER_WAIT:
        if ((nowMs - stateTimestampMs) >= DISCOVER_TIMEOUT_MS)
        {
            gprintf(gDBG, "[MASTER] Discovery window closed. Active peers: %u\r\n",
                    (uint32_t)countActivePeers());
            printPeerTable();
            webServerBroadcast(peerTable, peerCount, nowMs);
            webBroadcastMs = nowMs;
            pollPeerIndex  = 0U;
            pollTimerMs    = nowMs;
            masterState    = MASTER_STATE_IDLE;
        }
        break;

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_POLL_NEXT:
    {
        uint8_t nextIdx = findNextActivePeer(pollPeerIndex);
        if (nextIdx >= MAX_PEERS)
        {
            /* Entire poll cycle finished – push fresh data to browsers. */
            webServerBroadcast(peerTable, peerCount, nowMs);
            webBroadcastMs = nowMs;
            masterState    = MASTER_STATE_IDLE;
        }
        else
        {
            pollPeerIndex = nextIdx;
            masterState   = MASTER_STATE_POLL_SEND;
        }
        break;
    }

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_POLL_SEND:
        sendPacket(peerTable[pollPeerIndex].macAddr, PKT_POLL);
        stateTimestampMs = nowMs;
        masterState      = MASTER_STATE_POLL_WAIT;
        break;

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_POLL_WAIT:
        if ((nowMs - stateTimestampMs) >= POLL_PEER_TIMEOUT_MS)
        {
            pollPeerIndex++;
            masterState = MASTER_STATE_POLL_NEXT;
        }
        break;

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_IDLE:
        if ((nowMs - discoverTimerMs) >= DISCOVER_INTERVAL_MS)
        {
            masterState = MASTER_STATE_DISCOVER;
        }
        else if ((nowMs - pollTimerMs) >= POLL_INTERVAL_MS)
        {
            pollTimerMs   = nowMs;
            pollPeerIndex = 0U;
            masterState   = MASTER_STATE_POLL_NEXT;
        }
        else if ((nowMs - webBroadcastMs) >= WEB_BROADCAST_INTERVAL_MS)
        {
            /* Heartbeat: keeps browser table alive between poll cycles. */
            webServerBroadcast(peerTable, peerCount, nowMs);
            webBroadcastMs = nowMs;
        }
        break;

    /* ------------------------------------------------------------------ */
    case MASTER_STATE_ERROR:
        if ((nowMs - stateTimestampMs) >= INIT_RETRY_DELAY_MS)
        {
            gprintf(gDBG, "[MASTER] Retrying initialisation...\r\n");
            masterState = MASTER_STATE_INIT;
        }
        break;

    /* ------------------------------------------------------------------ */
    default:
        masterState = MASTER_STATE_INIT;
        break;
    }
}

// ============================================================================
// Arduino entry points
// ============================================================================

/**
 * @brief One-time setup: initialise the debug UART and print the boot banner.
 *
 * ESP-NOW and WiFi initialisation are deferred to MASTER_STATE_INIT so
 * that any failure is handled gracefully inside the state machine.
 */
void setup(void)
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    initUartBaud(gDBG, 115200U);
    esp_log_level_set("*", ESP_LOG_NONE);   /* suppress ESP-IDF WiFi/radio logs on UART */
    clear_screen(gDBG);
    gprintf(gDBG, "\r\n========================================\r\n");
    gprintf(gDBG, "  ESP-NOW Master Transceiver  v1.0\r\n");
    gprintf(gDBG, "  Home Monitor – discovery mode\r\n");
    gprintf(gDBG, "========================================\r\n\r\n");
}

/**
 * @brief Main loop: drives the master state machine on every iteration.
 *
 * No blocking calls are made here; all timing is millis()-based inside
 * masterProcess().
 */
void loop(void)
{
    masterProcess();
}

//=============================================================================
