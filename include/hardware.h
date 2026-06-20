/**
 * @file hardware.h
 * @brief Board-level hardware definitions for ESP32 DOIT DevKit v1.
 *
 * Defines logical UART channel identifiers, physical serial port objects,
 * and GPIO pin assignments. All gprintf library calls reference these
 * definitions at compile time.
 */

#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <driver/uart.h>

// ---------------------------------------------------------------------------
// Logical UART channel identifiers
// Must match the switch-case values used inside gprintf.cpp.
// ---------------------------------------------------------------------------

/** @brief Logical channel: debug / status output (USB-UART bridge, UART0). */
#define gDBG     0U

/** @brief Logical channel: RS485 (UART1 – placeholder, unused in this project). */
#define gRS485   1U

// ---------------------------------------------------------------------------
// Physical serial port objects
// ---------------------------------------------------------------------------

/** @brief HardwareSerial object mapped to gDBG. */
#define DBG      Serial

/** @brief HardwareSerial object mapped to gRS485. */
#define RS485    Serial1

// ---------------------------------------------------------------------------
// GPIO pin assignments – Debug UART (UART0, USB bridge on DOIT DevKit v1)
// ---------------------------------------------------------------------------

/** @brief UART0 TX pin (connected to USB-UART bridge). */
#define GPIO_DBG_TX       1

/** @brief UART0 RX pin (connected to USB-UART bridge). */
#define GPIO_DBG_RX       3

// ---------------------------------------------------------------------------
// GPIO pin assignments – RS485 UART (UART1 – placeholder, not wired)
// ---------------------------------------------------------------------------

/** @brief UART1 TX pin placeholder. */
#define GPIO_RS485_TX     17

/** @brief UART1 RX pin placeholder. */
#define GPIO_RS485_RX     16

/** @brief RS485 direction-control pin placeholder. */
#define GPIO_RS485_DIR     4

/** @brief ESP-IDF UART port number for RS485. */
#define RS485_UART_PORT    UART_NUM_1

//=============================================================================
