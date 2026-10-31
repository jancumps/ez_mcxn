#ifndef USB_CONSOLE_H
#define USB_CONSOLE_H
#include <stdint.h>
/* CDC instance 0 only. Call from main-loop context, not interrupts. */
int USB_ConsoleReady(void);
int USB_ConsoleWrite(const uint8_t *data, unsigned int length);
int USB_ConsoleGetChar(void); /* -1 if no input */
#endif
