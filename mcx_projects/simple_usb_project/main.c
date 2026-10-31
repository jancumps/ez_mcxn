#include <stdio.h>
#include <stdint.h>

#include "board.h"
#include "dual_usb_com.h"
#include "usb_console.h"

int main(void)
{
    BOARD_InitHardware();
    APPInit();

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    uint8_t wasReady = 0U;
    int pendingEcho = -1;

    while (1)
    {
        DualUsbCom_Poll();

        uint8_t ready = (uint8_t)USB_ConsoleReady();

        if (ready && !wasReady)
        {
            clearerr(stdout);
            clearerr(stderr);

            printf("\r\nMCXN236 USB console ready\r\n");
            printf("Type characters; they will be reported as hex codes.\r\n");
        }

        wasReady = ready;

        int ch;

        while ((ch = USB_ConsoleGetChar()) >= 0)
        {
            printf("RX: 0x%02X\r\n", (unsigned int)ch);
        }

        /* CDC1 echo lives in the application, not the USB library.
         * Retain a byte if the nonblocking TX FIFO is full.
         */
        if (pendingEcho < 0)
        {
            pendingEcho = USB_Port1GetChar();
        }
        if (pendingEcho >= 0 && USB_Port1Ready())
        {
            uint8_t byte = (uint8_t)pendingEcho;
            if (USB_Port1Write(&byte, 1U) == 1)
            {
                pendingEcho = -1;
            }
        }
    }
}
