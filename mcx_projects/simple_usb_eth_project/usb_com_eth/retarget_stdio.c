#include <errno.h>
#include <stdint.h>
#include "usb_console.h"
/* GCC/newlib stdout/stderr. Nonblocking; may return short writes if full. */
int _write(int fd, char *buf, int len)
{
    int n;
    if (fd != 1 && fd != 2) { errno = EBADF; return -1; }
    if (len <= 0) return 0;
    n = USB_ConsoleWrite((const uint8_t *)buf, (unsigned int)len);
    if (n == 0) { errno = EAGAIN; return -1; }
    return n;
}
