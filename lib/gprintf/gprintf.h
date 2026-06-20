#pragma once

#include "hardware.h"

#include <stdarg.h>
#include <stdint.h>

#ifdef WATCHDOG_ON
#  include <IWatchdog.h>
#endif

/** @brief UART transmit/receive ring-buffer size in bytes. */
#define BUFFER_SIZE        1024 *2

/** @brief ASCII escape character code. */
#define ESCAPE             0x1b

/** @brief Firmware version string. */
#define  VERSION "1.0"


/** @brief Logical UART channel identifier type. */
typedef uint32_t MYUART;

/**
 * @brief Verbosity levels for conditional diagnostic output.
 */
enum class VERBOSE {
    NONE,       /**< No output. */
    BASIC,      /**< Essential status messages only. */
    ELABORATE   /**< Full diagnostic detail. */
};

/** @brief gprintf parser state: processing literal characters. */
#define pfState_chars        0
/** @brief gprintf parser state: first character after '%' seen. */
#define pfState_firstFmtChar 1
/** @brief gprintf parser state: inside a multi-character format specifier. */
#define pfState_otherFmtChar 2

/**
 * @brief Initialises a UART channel to the requested baud rate.
 * @param[in] id    Logical UART channel (gDBG, gRS485, or gPSU).
 * @param[in] baud  Desired baud rate in bits per second.
 */
void     initUartBaud    (MYUART    id, uint64_t  baud);

/**
 * @brief Sends an ANSI clear-screen sequence to the specified UART channel.
 * @param[in] id  Logical UART channel.
 */
void     clear_screen    (MYUART    id);

/**
 * @brief Reads a floating-point value entered by the user on the specified UART.
 * @param[in] id  Logical UART channel.
 * @return Parsed float value.
 */
float    get_float       (MYUART    id);

/**
 * @brief Reads a hexadecimal value entered by the user on the specified UART.
 * @param[in] id  Logical UART channel.
 * @return Parsed 32-bit unsigned hex value.
 */
uint32_t get_hex         (MYUART    id);

/**
 * @brief Reads a decimal integer entered by the user on the specified UART.
 * @param[in] id  Logical UART channel.
 * @return Parsed signed 32-bit integer.
 */
int32_t  get_integer     (MYUART    id);

/**
 * @brief printf-style formatted output to the specified UART channel.
 * @param[in] id   Logical UART channel.
 * @param[in] fmt  printf-compatible format string.
 * @param[in] ...  Variadic arguments matching the format string.
 */
void     gprintf         (MYUART    id, const char    * fmt, ... );

/**
 * @brief Returns true if at least one character is available on the specified UART.
 * @param[in] id  Logical UART channel.
 * @return true if a character is waiting, false otherwise.
 */
bool     kbhit           (MYUART    id);

/**
 * @brief Non-blocking read of one byte from the specified UART.
 *
 * Returns 0 immediately if no byte is available. Callers must check kbhit()
 * first when a guaranteed byte is needed.
 *
 * @param[in] id  Logical UART channel.
 * @return Received byte, or 0 if no byte was available.
 */
uint8_t  read_ch         (MYUART    id);

/**
 * @brief Transmits one character to the specified UART channel.
 * @param[in] id  Logical UART channel.
 * @param[in] ch  Byte to transmit.
 */
void     put_ch          (MYUART    id, uint8_t          ch);

/**
 * @brief Reads a NUL-terminated line from the specified UART into a buffer.
 * @param[in]  id    Logical UART channel.
 * @param[out] line  Destination buffer.
 * @param[in]  size  Maximum number of bytes to store including the NUL terminator.
 */
void     get_line        (MYUART    id, char           * line   ,  const uint32_t  size );

/**
 * @brief Discards all pending bytes in the specified UART receive buffer.
 * @param[in] id  Logical UART channel.
 */
void     flushUart       (MYUART    id);

/**
 * @brief Prints a hex dump of a byte buffer in classic hex+ASCII format.
 * @param[in] id      Logical UART channel.
 * @param[in] buffer  Pointer to the data to dump.
 * @param[in] len     Number of bytes to dump.
 */
void     hex_dump        (MYUART    id, const uint8_t   * buffer,  const uint32_t    len);

/**
 * @brief Prints a compact two-column hex dump of a byte buffer.
 * @param[in] id      Logical UART channel.
 * @param[in] buffer  Pointer to the data to dump.
 * @param[in] len     Number of bytes to dump.
 */
void     hex2_dump       (MYUART    id, const uint8_t   * buffer,  const uint16_t    len);

/**
 * @brief Returns the length of a NUL-terminated C string.
 * @param[in] str  Pointer to the string.
 * @return Number of characters before the NUL terminator.
 */
uint32_t g_strlen        (const char      * str);

/**
 * @brief Converts an ASCII hex character ('0'-'9', 'A'-'F', 'a'-'f') to its nibble value.
 * @param[in] dum  ASCII character to convert.
 * @return Nibble value 0–15, or 0 for invalid input.
 */
uint8_t  AscToHex        (uint8_t           dum);

/**
 * @brief Converts an ASCII hex character to its nibble value (const overload).
 * @param[in] dum  ASCII character to convert.
 * @return Nibble value 0–15, or 0 for invalid input.
 */
uint8_t  AscToHex        (const uint8_t     dum);

/**
 * @brief Converts a single ASCII hex digit to its 4-bit numeric value.
 * @param[in] dum  ASCII hex digit.
 * @return Numeric value 0–15.
 */
uint8_t  asc2hex         (const uint8_t     dum);

/**
 * @brief Converts a lowercase ASCII letter to uppercase; passes other characters unchanged.
 * @param[in] ch  Input character.
 * @return Uppercase version of ch, or ch if not a lowercase letter.
 */
char     g_toupper       (char              ch);

/**
 * @brief Returns true if the given byte is an ASCII decimal digit ('0'–'9').
 * @param[in] ch  Byte to test.
 * @return true if ch is '0'–'9', false otherwise.
 */
bool     g_isdigit       (uint8_t           ch);

/**
 * @brief Appends the src string to dest and returns dest.
 * @param[in,out] dest  Destination NUL-terminated string.
 * @param[in]     src   Source NUL-terminated string to append.
 * @return Pointer to dest.
 */
char *   g_strcat        (char            * dest,
                          const char      * src);

/**
 * @brief Copies src into dest including the NUL terminator and returns dest.
 * @param[out] dest  Destination buffer.
 * @param[in]  src   Source NUL-terminated string.
 * @return Pointer to dest.
 */
char *   g_strcpy        (char            * dest,
                          const char      * src);

/**
 * @brief Compares two NUL-terminated strings lexicographically.
 * @param[in] str1  First string.
 * @param[in] str2  Second string.
 * @return Negative if str1 < str2, zero if equal, positive if str1 > str2.
 */
int32_t  g_strcmp        (const char      * str1,
                          const char      * str2);

/**
 * @brief Copies length bytes from source to dest.
 * @param[out] dest    Destination buffer.
 * @param[in]  source  Source buffer.
 * @param[in]  length  Number of bytes to copy.
 */
void     g_memcpy         (void            * dest,
                          void            * source,
                          uint32_t          length);

/**
 * @brief Fills length bytes of dest with the given value.
 * @param[out] dest    Destination buffer.
 * @param[in]  value   Byte value to write.
 * @param[in]  length  Number of bytes to fill.
 */
void     g_memset         (void           * dest,
                          uint8_t           value,
                          uint32_t          length);

/**
 * @brief Compares len bytes of two buffers.
 * @param[in] dst  First buffer.
 * @param[in] src  Second buffer.
 * @param[in] len  Number of bytes to compare.
 * @return true if the buffers are equal, false otherwise.
 */
bool     g_memcmp         (uint8_t        * dst,
                          uint8_t         * src,
                          uint32_t          len);

/**
 * @brief Converts a decimal ASCII string to a signed 64-bit integer.
 * @param[in] nptr  NUL-terminated decimal string.
 * @return Parsed value as long long int.
 */
int64_t       g_atoll     (const char     * nptr);

/**
 * @brief Converts an ASCII string to a signed 32-bit integer in the given numeric base.
 * @param[in] str   NUL-terminated string to parse.
 * @param[in] base  Numeric base (e.g. 10 for decimal, 16 for hex).
 * @return Parsed signed 32-bit value.
 */
int32_t  g_atoi           (char           * str, uint32_t base);

/**
 * @brief Writes a raw byte buffer to the specified UART channel.
 * @param[in] id         Logical UART channel.
 * @param[in] configPtr  Pointer to the data buffer to transmit.
 * @param[in] len        Number of bytes to transmit.
 */
void     usartWrBuffer    (MYUART           id,
                          uint8_t         * configPtr,
                          uint32_t          len);

/**
 * @brief Returns the current system tick count in milliseconds.
 * @return Tick count as a 64-bit unsigned integer.
 */
uint64_t get_tick_count   (void);

/**
 * @brief Formats a 64-bit unsigned integer as a decimal string in a static buffer.
 * @param[in] value  Value to format.
 * @return Pointer to a static NUL-terminated decimal string.
 */
char   * gprintflong      (uint64_t         value);

/**
 * @brief Formats a 64-bit unsigned integer as a hexadecimal string in a static buffer.
 * @param[in] value  Value to format.
 * @return Pointer to a static NUL-terminated hex string.
 */
char   * print64BitHex    (uint64_t         value) ;

/**
 * @brief Outputs message to gDBG if verbose_level meets or exceeds the active threshold.
 * @param[in] message       NUL-terminated message string.
 * @param[in] verbose_level Required verbosity level for the message to be printed.
 */
void     dspVerboseClass  (char *           message,
                           uint32_t         verbose_level);

/**
 * @brief Returns true if substr appears anywhere inside str.
 * @param[in] str     Haystack string.
 * @param[in] substr  Needle string.
 * @return true if substr is found within str.
 */
bool    isSubstring       (const char     * str, const char* substr);

/**
 * @brief Reads a signed 64-bit integer entered by the user on the specified UART.
 * @param[in] id  Logical UART channel.
 * @return Parsed signed 64-bit value.
 */
int64_t get_long          (MYUART           id);

/**
 * @brief Removes all occurrences of substr from str in-place.
 * @param[in,out] str     String to modify.
 * @param[in]     substr  Substring to remove.
 */
void    removeSubstring   (char           * str, const char* substr);

/**
 * @brief Returns true if str begins with prefix.
 * @param[in] str     String to test.
 * @param[in] prefix  Prefix to look for.
 * @return true if str starts with prefix.
 */
bool    startswith        (const char     * str, const char* prefix);

/**
 * @brief Prints the command prompt character sequence to gDBG.
 */
void    prompt            (void);

//==========[ the end ]========================================================
