#ifndef USB_CONSOLE_H
#define USB_CONSOLE_H
#include <stdint.h>
/* CDC instance 0 only. Call from main-loop context, not interrupts. */
int USB_ConsoleReady(void);
int USB_ConsoleWrite(const uint8_t *data, unsigned int length);
int USB_ConsoleGetChar(void); /* -1 if no input */
/* CDC1: nonblocking byte-oriented API; main-loop context only.
 * Write returns bytes queued (possibly 0); RX returns -1 when empty.
 * TX and RX FIFOs can overflow under sustained load.
 */
int USB_Port1Ready(void);
int USB_Port1Write(const uint8_t *data, unsigned int length);
int USB_Port1GetChar(void);
#endif
