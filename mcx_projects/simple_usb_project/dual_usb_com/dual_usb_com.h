#ifndef DUAL_USB_COM_H
#define DUAL_USB_COM_H

/* Initialize after BOARD_InitHardware(). */
void APPInit(void);
/* Call repeatedly from the bare-metal main loop. */
void DualUsbCom_Poll(void);

#endif
