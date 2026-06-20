#include "gprintf.h"
#include "hardware.h"

#define UINT_DIGITS 22

// https://arduino.stackexchange.com/questions/70955/esp32-hardware-serial-flow-control-and-full-espressif-arduino-esp32-support

#include <driver/uart.h>

/** ---------------------------------------------------------------------------
 * @brief Remove and reinstall the espressive rs485 driver
 *
 * @param baudrate  RS485 baudrate
 * #return Nothing
 */
void setupRS485(uint32_t baudrate)
{
    // configure UART for RS485
    const uart_port_t uart_num    = RS485_UART_PORT;
    uart_config_t     uart_config =
    {
        .baud_rate = (int) baudrate,      // BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122
    };

    // start arduino serial port
    RS485.begin(baudrate);
    // this is calculated
    RS485.setTimeout(1+(11000/baudrate) ); // 11000 is 11 symbols per byte * 1000 ms per second

    // drop the Arduino installed UART driver!
    // we will replace this with a 485 capable driver.
    uart_driver_delete(uart_num);

    vTaskDelay(pdMS_TO_TICKS(100));

    // Install UART driver (we don't need an event queue here)
    // In this example we don't even use a buffer for sending data.
    uart_driver_install (uart_num    , // uart number
                         BUFFER_SIZE , // rx_buffer_size
                         0,            // tx_buffer_size
                         0,            // queue size
                         NULL,         // queue handle
                         0);           // intr_alloc_flags
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configure UART parameters
    uart_param_config(uart_num, &uart_config);

    // Set UART1 pins(TX: IO23, RX: I022, RTS: IO18, CTS: IO19)
    uart_set_pin (uart_num,
                  GPIO_RS485_TX,  // ECHO_TEST_TXD,
                  GPIO_RS485_RX,  // ECHO_TEST_RXD,
                  GPIO_RS485_DIR, // ECHO_TEST_RTS,
                  UART_PIN_NO_CHANGE);

    // Set RS485 half duplex mode
    uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX);
}
/** ---------------------------------------------------------------------------
 * @brief initDebugBaud
 * @return
 */
void initUartBaud (MYUART id, uint64_t  baud)
{
    switch (id)
    {
#ifndef _QT_
    case gDBG   : DBG.begin  (baud, SERIAL_8N1, GPIO_DBG_RX, GPIO_DBG_TX ); break;
# ifdef gRS485
    case gRS485 : setupRS485(baud); break; //
# endif
# ifdef gPSU
    case gPSU   : PSU.begin  (baud, SERIAL_8N2, GPIO_PSU_RX, GPIO_PSU_TX ); break; 
#endif    
    default     : break;
#else
    if (baud) {}
    if (id) {}
#endif
    }
}
/** ---------------------------------------------------------------------------
 * @brief Returns true if at least one byte is waiting in the specified UART receive buffer.
 * @param[in] id  Logical UART channel.
 * @return true if a character is available, false otherwise.
 */
bool kbhit(MYUART id)
{
/*/
#ifndef _QT_
    if (serialEventRun != nullptr)
    {
         serialEventRun();
    }
#endif
*/
    int32_t amount = 0;

    switch (id)
    {
#ifndef _QT_
    case gDBG   : amount = DBG.available (); break;
# ifdef gRS485
    case gRS485 : amount = RS485.available(); break;
# endif
# ifdef gPSU
    case gPSU   : amount = PSU.available (); break;
# endif
#endif
    default : break;
    }
    if (amount > 0)
        return true;
    return false;
}
/** ---------------------------------------------------------------------------
 * @brief  Display the default prompt on the screen
 */
void prompt (void)
{
    gprintf (gDBG, "\r\n> ");
}
/** -------------------------------------------------- -------------------------
 * @brief Non-blocking read of one byte from the specified UART.
 *
 * Returns 0 immediately if no data is available. Callers must check kbhit()
 * first when a guaranteed byte is expected.
 *
 * @param[in] id  Logical UART channel.
 * @return Received byte, or 0 if no byte was available.
 */
uint8_t read_ch (MYUART id)
{
    while (kbhit (id) == false)
    {
        delay (1);
        //put_ch (id, '.');
#ifdef WATCHDOG_ON
        // start the watchdog
        IWatchdog.begin( IWDG_TIMEOUT_MAX/10);
#endif
    }
    uint8_t ch = 0;
    switch (id)
    {
#ifndef _QT_
    case gDBG  : if (DBG.available())   { ch = (uint8_t)DBG.read();   } break;
# ifdef gRS485
    case gRS485: if (RS485.available()) { ch = (uint8_t)RS485.read(); } break;
# endif    
# ifdef gPSU
    case gPSU  : if (PSU.available())   { ch = (uint8_t)PSU.read();   } break;
# endif
#endif
    default: break;
    }
    return ch;
}
/** ---------------------------------------------------------------------------
 * @brief put_ch
 * @param id Stream destination
 * @param ch Character to be send
 */
void put_ch (MYUART id, uint8_t  ch)
{
    uint8_t tmp = ch;
    switch (id)
    {
#ifndef _QT_
    case gDBG   : DBG.write  (&tmp,1); break;
# ifdef gRS485
    case gRS485 : RS485.write(&tmp,1); break;
# endif    
# ifdef gPSU
    case gPSU   : PSU.write  (&tmp,1); break;
# endif    
#else
    if (tmp) {}
#endif
    }
}
/** ---------------------------------------------------------------------------
 * @brief flushUart Remove all character from the uart receive queue
 */
void flushUart  (MYUART id)
{
    // volatile uint8_t ch;
    switch(id)
    {
#ifndef _QT_
        case gDBG   : DBG.flush   (false); break;
# ifdef gRS485
        case gRS485 : RS485.flush (false); break;
# endif
# ifdef  gPSU
        case gPSU   : PSU.flush   (false); break;
# endif
#endif
        default: return;
    }
}
/** -------------------------------------------------------------------------
 * @brief Clear the screen assuming you have at VT100 compatible terminal
 * @param id
 */
void clear_screen (MYUART id)
{
#ifdef _QT_
    switch (id)
    {
    case gDEBUG : printf ("\f"); break;
    }
#else
    switch (id)
    {
    case gDBG   : DBG.print ("\f") ; break;
# ifdef gRS485
    case gRS485 : RS485.print ("\f") ; break;
# endif    
# ifdef gPSU
    case gPSU   : PSU.print ("\f") ; break;
# endif
    }
#endif
}
/** ---------------------------------------------------------------------------=
 * @brief g_toupper
 * @param ch
 * @return
 */
char  g_toupper (char ch)
{
    if ((ch >= 'a') && (ch <='z'))
        return (ch - 0x20);
    else
        return (ch);
}
/** ---------------------------------------------------------------------------
 * @brief g_strlen  Simple string length implementation to reduce code and
 * not to use glibc
 * @param str  Pointer to the string to evaluate
 * @return  String length in bytes
 */
uint32_t g_strlen(const char *str)
{
    const char *s;

    for (s = str; *s; ++s)
        ;
    return (s - str);
}
/** ---------------------------------------------------------------------------
 * @brief g_isdigit
 * @param ch  Character to be scrutinised
 * @return    True if digit else false
 */
bool g_isdigit (uint8_t ch)
{
    return((ch >= '0') && (ch <= '9'));
}
/** ---------------------------------------------------------------------------
 * @brief g_strcat Simple string concatenation implementation to reduce code and
 * not to use glibc
 * @param dest
 * @param src
 * @return
 * @note  No checking is done, and this could lead to memory override
 */
char * g_strcat(char *dest, const char *src)
{
    uint32_t i,j;

    // find the destination end
    for (i = 0; dest[i] != '\0'; i++) {}

    // copy from source to destination end, until the source end
    for (j = 0; src[j] != '\0'; j++)
        dest[i+j] = src[j];

    // null terminate string
    dest[i+j] = '\0';
    return dest + i +j;
}
/** ---------------------------------------------------------------------------
 * @brief g_strcpy
 * @param dest Pointer to memory to copy bytes
 * @param src  Pointer to memory to read bytes
 * @return True on success
 */
char * g_strcpy (char *dest, const char *src)
{
    *dest = 0;
    dest = g_strcat(dest,src);
    return dest;
}
/** ---------------------------------------------------------------------------
 * @brief g_memcmp
 * @param dest Pointer to memory to copy bytes
 * @param src  Pointer to memory to read bytes
 * @return True on success
 */
bool g_memcmp  (uint8_t * dst, uint8_t * src, uint32_t len)
{
    uint32_t error = 0, i;

    for (i=0; i < len; i++)
    {
        if (*dst != *src)
           error++;
        dst++;
        src++;
    }
//    if (error > 0)
//         gprintf (gDEBUG, " error Cnt = %u", error);
    return (error == 0);
}
/** ---------------------------------------------------------------------------
 *  This function compares the two input strings `str1` and `str2` lexicographically.
 * 
 *  "Lexicographically" refers to the ordering of words or strings based on the 
 *  alphabetical order of their individual characters, similar to how words are
 *  arranged in a dictionary.
 *
 *  Return a negative value if str1 is less than str2
 *  Return 0 if str1 is equal to str2
 *  Return a positive value if str1 is greater than str2
 *
 * @param str1 The first string to be compared.
 * @param str2 The second string to be compared.
 * @return int
 */
int32_t g_strcmp(const char* str1, const char* str2)
{
    int32_t i = 0;
    while (str1[i] && str2[i] && str1[i] == str2[i]) {
        i++;
    }
    return str1[i] - str2[i];
}
/** ---------------------------------------------------------------------------
 * @brief g_memcpy
 * @param dest
 * @param source
 * @param length
 */
void     g_memcpy   (void            * dest,
                     void            * source,
                     uint32_t          length )
{
    uint8_t * dstPtr = (uint8_t *) dest;
    uint8_t * srcPtr = (uint8_t *) source;

    for (uint32_t i = 0; i < length; *dstPtr++ = *srcPtr++, i++);
}
/** ---------------------------------------------------------------------------
 * @brief g_memset
 * @param dest
 * @param value
 * @param length
 */
void     g_memset  (void            * dest,
                    uint8_t           value,
                    uint32_t          length )
{
    uint8_t * ptr = (uint8_t *) dest;

    for (uint32_t i = 0; i < length; *ptr++ = value, i++);
}
/** ---------------------------------------------------------------------------
 * @brief g_atoi Simple string to integer conversion
 * @param   str     Pointer to the string to convert
 * @return  integer Value presented in the string
 * @note No checking is done , this function assumes all is funky
 */
int32_t g_atoi(char *str, uint32_t base)
{
    bool    sign = false;
    int32_t res  = 0;
    int32_t i;

    // Iterate through all characters of input string and update result
    for (i = 0; str[i] != '\0'; ++i)
    {
        // very basic test
        if (g_isdigit(str[i]))
        {
            res = res * base + asc2hex(str[i]);
        }
        else if (str[i] == '-' )
            sign = true;
    }
    if (sign)
        res *= -1;
    // return result.
    return res;
}
/** ---------------------------------------------------------------------------
 * @brief g_atoll Simple string to long integer conversion
 * @param   str     Pointer to the string to convert
 * @return  integer Value presented in the string
 * @note No checking is done , this function assumes all is funky
 */
int64_t g_atoll (const char *nptr)
{
  return strtoll (nptr, (char **) NULL, 10);
}
/** ---------------------------------------------------------------------------
 * @brief g_atof Simple string to float conversion
 * @param str  Pointer to the string to be converted
 * @return     String value as a double (real)
 * @note No checking is done , this function assumes all is funky
 */
double g_atof(char *str)
{
    double  a = 0.0;
    int32_t e = 0;
    int32_t c;

    while ((c = *str++) != '\0' && g_isdigit(c))
    {
        a = a*10.0 + (c - '0');
    }
    if (c == '.')
    {
        while ((c = *str++) != '\0' && g_isdigit(c))
        {
            a = a*10.0 + (c - '0');
            e = e-1;
        }
    }
    if (c == 'e' || c == 'E')
    {
        int32_t sign = 1;
        int32_t i    = 0;
        c = *str++;
        if (c == '+')
            c = *str++;
        else if (c == '-')
        {
            c = *str++;
            sign = -1;
        }
        while (g_isdigit(c))
        {
            i = i*10 + (c - '0');
            c = *str++;
        }
        e += i*sign;
    }
    while (e > 0)
    {
        a *= 10.0;
        e--;
    }
    while (e < 0)
    {
        a *= 0.1;
        e++;
    }
    return a;
}
/** ---------------------------------------------------------------------------
 * @brief  This method is kept for debugging purposes. This Dump the Memory
 * with address and values as hex byte values
 * @param  buffer    A pointer to the memory to be displayed in dump fashion
 * @param  len       The length to be displayed
**/
void hex_dump (MYUART            id,
               const uint8_t   * buffer,
               const uint32_t    len)
{
    uint32_t    isp_counter = 0;
    uint32_t    value, max = len/4;
    uint32_t  * address = (uint32_t *) buffer;

    prompt();
    gprintf (id, "Dump %d %04X bytes ",(int)len, (int)len);
    //if the dump is to short
    if ((max*4) != len)
        max++;
    // verify that we have a uart to write to
    for (isp_counter = 0; isp_counter < max; isp_counter++)
    {
        if ((isp_counter & 0x7) == 0)
        {
            // ram is always from zero
            gprintf (id,"\r\n  %08X  ", address );
            // break;
        }
        else if ((isp_counter & 0x3) ==0)
        {
            gprintf (id,"  " );
        }
        value = *address++;

        gprintf (id, " %08X",value);
    }
    if ((isp_counter & 0x7) != 0)
        gprintf (id, "\r\n");
    else
        gprintf (id, "  ");
}
/** ---------------------------------------------------------------------------
 * @brief  This method is kept for debugging purposes. This Dump the Memory
 * with address and values as hex byte values
 * @param  buffer    A pointer to the memory to be displayed in dump fashion
 * @param  len       The length to be displayed
**/
void hex2_dump (MYUART            id,
                const uint8_t   * buffer,
                const uint16_t    len)
{
    if (buffer == nullptr)
        return;
    uint32_t    isp_counter = 0;
    uint8_t     value;
    uint32_t    max  = len;
    uint8_t   * address =  (uint8_t *)buffer;

    gprintf (id, "> Dump %d %04X bytes ",(int)len, (int)len);
    // if the dump is to short
    // verify that we have a uart to write to
    for (isp_counter = 0; isp_counter < max; isp_counter++)
    {
        if ((isp_counter & 0x01f) == 0)
        {
            // ram is always from zero
            gprintf (id, "\r\n%08X  ", address );
            // break;
        }
        else if ((isp_counter & 0x3) ==0)
        {
            gprintf (id," " );
        }
        value = *address++;

        gprintf (id, " %02X", value);
    }
    if ((isp_counter & 0x1F) != 0)
        gprintf (id, "\r\n");
    else
        gprintf (id, "  ");
}
/** ----------------------------------------------------------------------------
 * @brief  Convert ASC to Hex
 * @param  dum The byte value to be converted
 * @return hex value of the data parameter
**/
uint8_t asc2hex (const uint8_t dum)
{
    // check for decimal values
    if (dum <= '9') return (dum - '0');
    // check for upper case values
    if (dum <= 'F') return (dum - 'A' + 0x0a);
    // check for lower case values
    if (dum <= 'f') return (dum - 'a' + 0x0a);
    // else just echo input data
    return (dum);
}
/** ---------------------------------------------------------------------------
 * @brief  hex to ASCII
 * @param  dum  Nibble to be converted
 * @return ASCII character
 * @note The user must make sure the top nibble is zero
**/
uint8_t hex2asc (const uint8_t dum)
{
    // check for decimal values
    if (dum <= 9) return (dum + '0');
    return (dum -10 + 'A');
}
/** ---------------------------------------------------------------------------
 * @brief  Is Valid ASCII (sort of)
 * @param  data_val The character under question
 * @return Either a printable character or '.'
 * @todo This is not really acceptable.
**/

uint8_t is_ascii (const uint8_t  data_val)
{
    if ((data_val >= 0x1f) && (data_val <= 127))
        return data_val;
    else
        return '.';
}
/** ----------------------------------------------------------------------------
 * @brief get_line Line Editor
 * @param id
 * @param line
 * @param size
 */
void get_line ( MYUART id, char * line , const uint32_t size )
{
    int32_t          i = 0;

    do
    {
        char         ch = 0;

        switch (ch = read_ch(id))
        {
        case 0x00 :  // no data available (read_ch is non-blocking)
            break;
        case 0x0a :
        case 0x0d :
        case 0xe1 :  // special for esp32-s3 in platformio
            // end of line
            *line = 0;
            return;
        case 0x08 :
        case 127  :  // linux backspace
            // can only delete if anything was typed
            if (i > 0)
            {
                put_ch(id, 0x08);
                put_ch(id, ' ');
                put_ch(id, 0x08);
                // remove warning
                if (*line--) {}
            }
            break;
        default  :
            *line++ = ch;
            if ((ch >= '0') && (ch <= 'z'))
               put_ch(id, ch);
            else
               gprintf (id, "0x%02X", ch);
            break;
        }
        i++;
    } while (i < (int32_t)size);
}
/** ----------------------------------------------------------------------------
 * @brief get_float input a float value
 * @param id
 * @return
 */
float get_float (MYUART id)
{
    char     Temp [20];

    get_line (id, Temp,19);
    return (g_atof (Temp));
}
/** ----------------------------------------------------------------------------
 * @brief get_integer input a integer value
 * @param id
 * @return
 */
int32_t get_integer(MYUART id)
{
    char     Temp [20];

    get_line (id, Temp,19);
    return g_atoi(Temp, 10);
}
/** ---------------------------------------------------------------------------
 * @brief get_long_integer input a integer value
 * @param id
 * @return
 */
int64_t get_long (MYUART id)
{
    char     Temp [25];

    get_line (id, Temp,24);
    return (int64_t)g_atoll(Temp);
}
/** ----------------------------------------------------------------------------
 * \brief  Convert ASC to Hex
 * \param  dum The byte value to be converted
 * \return hex value of the data parameter
**/
uint8_t AscToHex (const uint8_t dum)
{
    // check for decimal values
    if (dum <= '9') return (dum - '0');
    // check for upper case values
    if (dum <= 'F') return (dum - 'A' + 0x0a);
    // check for lower case values
    if (dum <= 'f') return (dum - 'a' + 0x0a);
    // else just echo input data
    return (dum);
}
/** ---------------------------------------------------------------------------
 * @brief get_hex
 * @param id
 * @return
 */
uint32_t get_hex (MYUART id)
{
    char     Temp [20] = {0};
    uint32_t i=0, j=0, k = 0;

    get_line (id, Temp,19);

    i = g_strlen (Temp);
    while (j < i)
    {
        k *= 16;
        k += AscToHex(Temp[j++]);
    }
    return k;
}
/** ---------------------------------------------------------------------------
 * @brief gprintf Function Gerries limited but enhanced printf. This is
 * by no means a fully functional printf, but you may be delighted to see its
 * ability
 * example gprintf ("> Text %-10s, %6.3F %10b %10o %7.2f ", etc
 *     %d, u, b, o, x(X)  (integer, unsigned integer, binary, octal, hexadecimal
 *     %f, %lk.mnf  (%12.6f), %g, %e, %E,
 *     %*
 *     %c    Character
 *     %s    String
 *     %S    Display only printable chars, else '.'s
 *     %+    Force a '+' to be displayed (only integer)
 *     %-    left/right column alignment
 *     %# -- not implemented
 *     %ll-- not implemented
 * @param id     UART Number to be used
 * @param fmt    Pointer to the argument
 * @return       The actual amount of characters used to create the string
 */
void gprintf (MYUART id, const char *fmt, ... )
{
#ifdef _QT_
    if (id) {}
    if (fmt) {}
#else
    va_list           args;
    const char      * w;
    char              c, ch;
    int32_t           state=0;
    int32_t           fmtLeadingZero=0;
    int32_t           fmtLong=0;
    int32_t           fmtBeforeDecimal=-1;
    int32_t           fmtAfterDecimal=-1;
    int32_t           fmtBase=10;
    int32_t           fmtSigned=0;
    int32_t           fmtPlus=0;
    int32_t           fmtLongLong= 0;
    int32_t           fmtCase = 0;    // For hex format, if 1, A-F, else a-f.
    int32_t           j       = 0;
    uint32_t          index   = 0;
    int32_t           dumint  = 0;
    char              flt_buffer [30];
    char              format [20];
    int32_t           format_index=0;
    size_t            i = 0;
    volatile double   v;
    uint32_t          k;
    char              dum [20];
    static unsigned char buffer[BUFFER_SIZE];

    memset(buffer,0,BUFFER_SIZE);

    va_start (args, fmt);

    w = fmt;

    state = pfState_chars;

    while(0 != (c = *w++))
    {
        switch(state)
        {
        case pfState_chars:
            if(c == '%')
            {
                // default for integer
                fmtLeadingZero = 0;
                fmtLong = 0;
                fmtBase = 10;
                fmtSigned = 1;
                fmtCase = 0; // Only %X sets this.
                fmtPlus = 0;
                fmtBeforeDecimal = -1;
                fmtAfterDecimal = -1;
                state = pfState_firstFmtChar;
                //----------
                format_index = 0;
            }
            else
            {
                buffer[index]= c;
                if (index < (BUFFER_SIZE -2)) index++;
            }
            break;

        case pfState_firstFmtChar:
            if(c == '0')
            {
                fmtLeadingZero = 1;
                state = pfState_otherFmtChar;
                format [format_index++] = c;
            }
            else if(c == '%')
            {
                buffer[index]= c;
                if (index < (BUFFER_SIZE -2)) index++;
                state = pfState_chars;
            }
            else if (c == '#')
            {
                // not implemented ignore
                state = pfState_otherFmtChar;
            }
            else if (c == '+')
            {
                fmtPlus = 1;
                state = pfState_otherFmtChar;
            }
            else if (c == '-')
            {
                fmtSigned = 0;
                state = pfState_otherFmtChar;
            }
            else
            {
                state = pfState_otherFmtChar;
                goto otherFmtChar;
            }
            break;

        case pfState_otherFmtChar:
otherFmtChar:
            format [format_index++] = c;
            ///  This get the precision from the argument list
            if (c == '*')
            {
                // get the value from the argument list
                v = va_arg(args,int);

                // still before decimal
                if(fmtAfterDecimal < 0)
                {
                    if(fmtBeforeDecimal < 0)
                        fmtBeforeDecimal = 0;
                    else
                        fmtBeforeDecimal *= 10;
                    fmtBeforeDecimal += v;
                }
                else
                    fmtAfterDecimal = (fmtAfterDecimal * 10) + v;
            }
            else if(c == '.')
                fmtAfterDecimal = 0;
            else if('0' <= c && c <= '9')
            {
                c -= '0';
                if(fmtAfterDecimal < 0)             // still before decimal
                {
                    if(fmtBeforeDecimal < 0)
                        fmtBeforeDecimal = 0;
                    else
                        fmtBeforeDecimal *= 10;
                    fmtBeforeDecimal += c;
                }
                else
                    fmtAfterDecimal = (fmtAfterDecimal * 10) + c;
            }
            else if(c == 'l')
            {
                if (fmtLong)
                    fmtLongLong = 1;
                else
                    fmtLong = 1;
            }
            else
                // we're up to the letter which determines type
            {
                // Get the type , and process it
                switch(c)
                {
                case 'f':  // do float print
                    // Get the value
                    v = va_arg(args,double);
                    format [format_index++] = 0;
                    dum[0] = '%';
                    dum[1] = 0;
                    strcat (dum,format);
                    //snpintf (flt_buffer,sizeof(flt_buffer) -1, dum,v);
                    //dtostrf(v, (fmtBeforeDecimal + fmtAfterDecimal + 1),fmtAfterDecimal, flt_buffer);
                    dtostrf(v, fmtBeforeDecimal,fmtAfterDecimal, flt_buffer);
                    for (k=0; k < strlen(flt_buffer);k++)
                    {
                        buffer[index]= flt_buffer[k];
                        if (index < (BUFFER_SIZE -2)) index++;
                    }

                    state = pfState_chars;
                    break;
                case 'd':  //
                case 'i':  //   doIntegerPrint:
doIntegerPrint:
                {
                    int64_t        v;
                    uint64_t       p;        // biggest power of fmtBase
                    uint64_t       vShrink;  // used to count digits
                    int32_t        sign;
                    int32_t        digitCount;
                    bool           islong = false;

                    // Get the value from the argument list
                    if (fmtLongLong == 1)
                    {
                        if (fmtSigned)
                            v = va_arg(args,long long);
                        else
                            v = va_arg(args,unsigned long long);
                        islong = true;
                    }
                    else if(fmtLong == 1)
                    {
                        if (fmtSigned)
                            v = va_arg(args,long);
                        else
                            v = va_arg(args,unsigned long);
                        islong = true;                      
                    }
                    else
                    {
                        if (fmtSigned)
                            v = va_arg(args,int);
                        else
                            v = va_arg(args,unsigned int);
                    }

                    // Strip sign
                    int topBitPosition = 31;
                    if (islong) 
                        topBitPosition = 63;

                    sign = 0;
                    if( fmtSigned && (v & ((uint64_t) 1 << topBitPosition)) )
                    {
                        v = ~v + 1;
                        sign = 1;
                    }

                    // Count digits, and get largest place value
                    vShrink = v;
                    p = 1;
                    digitCount = 1;
                    while( (vShrink = vShrink / fmtBase) > 0 )
                    {
                        digitCount++;
                        p *= fmtBase;
                    }

                    // Print leading characters & sign
                    fmtBeforeDecimal -= digitCount;
                    if(fmtLeadingZero)
                    {
                        if(sign)
                        {
                            buffer[index]= '-';
                            if (index < (BUFFER_SIZE -2)) index++;
                            fmtBeforeDecimal--;
                        }
                        for (j = 0; j < fmtBeforeDecimal;j++)
                        {
                            buffer[index]= '0';
                            if (index < (BUFFER_SIZE -2)) index++;
                        }
                    }
                    else
                    {
                        if((sign == 1 )|| (fmtPlus == 1))
                            fmtBeforeDecimal--;
                        for (j = 0; j < fmtBeforeDecimal;j++)
                        {
                            buffer[index]= ' ';
                            if (index < (BUFFER_SIZE -2)) index++;
                        }
                        if(sign)
                        {
                            buffer[index]= '-';
                            if (index < (BUFFER_SIZE -2)) index++;
                        }
                        else if (fmtPlus)
                        {
                            buffer[index]= '+';
                            if (index < (BUFFER_SIZE -2)) index++;
                        }

                    }
                    // Print numbery parts
                    while(p)
                    {
                        unsigned char d = 0;
                        uint64_t      g = 0;

                        g = v / p;
                        d = g & 0xFF;
                        if(d > 9)
                        {
                            if (fmtCase == 1)
                                d +=  'A' - 10;
                            else
                                d +=  'a' - 10;
                        }
                        else d += '0';
                        buffer[index]= d;
                        if (index < (BUFFER_SIZE -2)) index++;
                        v = v % p;
                        p = p / fmtBase;
                    }
                }
                    // state = pfState_chars;
                    break;

                case 'u':
                    fmtSigned = 0;
                    goto doIntegerPrint;
                case 'b':
                    fmtSigned = 0;
                    fmtBase = 2;
                    goto doIntegerPrint;
                case 'o':
                    fmtSigned = 0;
                    fmtBase = 8;
                    goto doIntegerPrint;
                case 'x':
                    fmtCase = 0;
                    fmtSigned = 0;
                    fmtBase = 16;
                    goto doIntegerPrint;
                case 'X':
                    fmtSigned = 0;
                    fmtBase = 16;
                    fmtCase = 1;
                    goto doIntegerPrint;
                case 'B':
                    v = va_arg(args,int);
                    if ( v == 0)
                    {
                        buffer[index]= 'F'; if (index < (BUFFER_SIZE -2)) index++;
                        buffer[index]= 'a'; if (index < (BUFFER_SIZE -2)) index++;
                        buffer[index]= 'l'; if (index < (BUFFER_SIZE -2)) index++;
                        buffer[index]= 's'; if (index < (BUFFER_SIZE -2)) index++;
                        buffer[index]= 'e'; if (index < (BUFFER_SIZE -2)) index++;
                    }
                    else
                    {
                        buffer[index]= 'T'; if (index < (BUFFER_SIZE -2)) index++;
                        buffer[index]= 'r'; if (index < (BUFFER_SIZE -2)) index++;
                        buffer[index]= 'u'; if (index < (BUFFER_SIZE -2)) index++;
                        buffer[index]= 'e'; if (index < (BUFFER_SIZE -2)) index++;
                        //buffer[index]= ' '; if (index < (BUFFER_SIZE -2)) index++;
                    }
                    break;
                case 'c':

                    for (j = 0; j < fmtBeforeDecimal-1;j++)
                    {
                        buffer[index]= ' ';
                        if (index < (BUFFER_SIZE -2)) index++;
                    }
                    buffer[index]= va_arg(args,int);
                    if (index < (BUFFER_SIZE -2)) index++;
                    break;
                case 'S':
                case 's':
                {
                    char *s;

                    s = va_arg(args,char *);
                    dumint = fmtBeforeDecimal - (strlen(s) & 0xFFFF);
                    // space before
                    if (fmtSigned > 0)
                    {
                        if (dumint > 0)
                        {
                            for (j = 0;
                                 j < dumint;
                                 j++)
                            {
                                buffer[index]= ' ';
                                if (index < (BUFFER_SIZE -2)) index++;
                            }
                        }
                    }
                    // the string itself
                    while(*s)
                    {
                        if (c == 's')
                        {
                            buffer[index]= *s++;
                            if (index < (BUFFER_SIZE -2)) index++;
                        }
                        else
                        {
                            // valid asci
                            /// @todo write better code here
                            if ((*s > 0x1f) && (*s < 'z'))
                            {
                                buffer[index] = *s++;
                                if (index < (BUFFER_SIZE -2)) index++;
                            }
                            else
                            {
                                s++;
                                buffer[index] = '.';
                                if (index < (BUFFER_SIZE -2)) index++;
                            }
                        }
                    }
                    // spaces after
                    if (fmtSigned == 0)
                    {
                        if (dumint > 0)
                        {
                            for (j = 0;
                                 j < dumint;
                                 j++)
                            {
                                buffer[index]= ' ';
                                if (index < (BUFFER_SIZE -2)) index++;
                            }
                        }
                    }
                    break;
                }
                    break;
                } // switch last letter of fmt
                state=pfState_chars;
            }
            break;
        } // switch
    } // while chars left /

    // Buffer now contains the string, just add zero termination
    buffer[index]= 0;
    uint32_t len = g_strlen((char *)buffer);

   switch (id)
    {
    case gDBG   : DBG.write(buffer, len);                      break;
    case gRS485 : if (uart_write_bytes (1, buffer, len)) {}    break;
#ifdef  gPSU
    case gPSU   : PSU.write (buffer,len);   break;
#endif
    default     : break;
    }
/*
    for (i = 0; i < g_strlen((char *)buffer); i ++)
    {
        ch = buffer[i];
        switch (ch)
        {
        case '\f':
           clear_screen(id);
           break;
        case '\r':
        case '\n' :
        default :
            put_ch (id, ch);
            break;
       }
    }
       */
    va_end(args);
    if (serialEventRun != nullptr)
    {
        serialEventRun();
    }
#endif
}
/** ---------------------------------------------------------------------------
 * @brief usartWrBuffer Write an array to the serial device id
 * @param ptr  Pointer to memory locating the array to be send
 * @param len  Amount of characters to be send
*/
void     usartWrBuffer (MYUART            id,
                        uint8_t         * ptr,
                        uint32_t          len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        put_ch (id, *ptr++);
    }
    flushUart(id);
}
/** ---------------------------------------------------------------------------
 * @brief Get the tick count object
 *
 * @return uint64_t
 */
uint64_t get_tick_count (void)
{
    return millis ();
}
/** ----------------------------------------------------------------------------
 * @brief gprintflong  Alternative to snprintf llu not working
 *
 * @param   value    Huge number
 * @return  char*
 */
char * gprintflong (uint64_t value)
{
  /* Room for UINT_DIGITS digits and '\0' */
  static char   buf[UINT_DIGITS + 1];
  char        * p = buf + UINT_DIGITS;	// points to terminating '\0' 
  do {
    *--p = '0' + (value % 10);
    value /= 10;
  } while (value != 0);
  return p;
}
/** ---------------------------------------------------------------------------
 * @brief print64BitHex
 * @param value
 */
char * print64BitHex (uint64_t value) 
{
    static char   buffer[256];       
    snprintf (buffer, 256, "%016llX",value);
    return buffer; 
}
/** ---------------------------------------------------------------------------
 * @brief dspVerboseClass 
 * @param message
 * @param value
 */
void dspVerboseClass (char * message, uint32_t verbose_level)
{
    gprintf(gDBG, "\r\n  %-20s::setVerboseLevel(%d)", message, verbose_level);
}

/** ---------------------------------------------------------------------------
 * @brief isSubstring Check if a string is a substring of another string
 * @param str The string to search in
 * @param substr The substring to search for
 * @return True if the substring is found, false otherwise
 */
bool isSubstring(const char* str, const char* substr)
{
    uint32_t strLen = 0;
    uint32_t subLen = 0;
    uint32_t i;

    for (strLen = 0; str[strLen]    != '\0'; strLen++) {}
    for (subLen = 0; substr[subLen] != '\0'; subLen++) {}

    if (subLen > strLen)
        return false;

    for (i = 0; i <= (strLen - subLen); i++)
    {
        bool found = true;

        for (uint32_t k = 0; k < subLen; k++)
        {
            if (str[i + k] != substr[k])
            {
                found = false;
                break;
            }
        }

        if (found)
            return true;
    }

    return false;
}

/** ---------------------------------------------------------------------------
 * @brief removeSubstring Remove all occurrences of a substring from a string
 * @param str The string to remove from
 * @param substr The substring to remove
 *
void removeSubstring(char* str, const char* substr)
{
    uint32_t i, j, k;
    uint32_t substrLen = strlen(substr);

    // iterate through the destination string
    for (i = 0; str[i] != '\0'; i++)
    {
        // check if the substring matches at the current position
        if (isSubstring(&str[i], substr))
        {
            // shift the remaining characters to the left
            for (j = i, k = i + substrLen; str[k] != '\0'; j++, k++)
            {
                str[j] = str[k];
            }
            str[j] = '\0';
            i--; // adjust the index to recheck the current position
        }
    }
}*/

/** ---------------------------------------------------------------------------
 * @brief startswith Check if a string starts with a given prefix
 * @param str The string to check
 * @param prefix The prefix to compare with
 * @return True if the string starts with the prefix, false otherwise
 */
bool startswith(const char* str, const char* prefix)
{
    uint32_t i;

    // iterate through the prefix string
    for (i = 0; prefix[i] != '\0'; i++)
    {
        // check if the characters match
        if (str[i] != prefix[i])
        {
            return false;
        }
    }

    return true;
}
//=======[ the end ]===============================================================
