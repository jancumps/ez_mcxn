#ifndef USB_COM_ETH_H
#define USB_COM_ETH_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Unified startup function.
 * Initializes the USB controller, composite devices (CDC-ACM Serial + CDC-NCM Ethernet),
 * the lwIP TCP/IP stack (192.168.7.2), and the embedded DHCP server.
 * Call once in main() after BOARD_InitHardware() and SysTick_Config().
 */
void UsbEthernet_Init(void);

/**
 * @brief Unified polling function.
 * Moves incoming and outgoing packets between the USB controller and lwIP,
 * and drives lwIP network protocol timers.
 * Call continuously in your bare-metal while(1) loop.
 */
void UsbEthernet_Poll(void);

/* Low-level component functions (available if fine-grained control is needed): */
void APPInit(void);
void DualUsbCom_Poll(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_COM_ETH_H */
