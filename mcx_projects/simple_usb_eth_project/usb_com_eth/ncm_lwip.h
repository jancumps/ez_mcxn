#ifndef NCM_LWIP_H
#define NCM_LWIP_H

#include <stdint.h>
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize lwIP stack and CDC-NCM Ethernet network interface.
 *
 * Configures the interface with IP 192.168.7.2 / 255.255.255.0 and starts
 * the embedded DHCP server to automatically assign 192.168.7.1 to the connected host.
 */
void NCM_LwipInit(void);

/**
 * @brief Poll lwIP timeouts and update link status.
 * Must be called periodically from the main loop.
 */
void NCM_LwipPoll(void);

/**
 * @brief Deliver a received Ethernet frame to lwIP.
 */
void NCM_LwipReceive(const uint8_t *frame, uint16_t length);

/**
 * @brief Get the lwIP netif structure for the NCM Ethernet interface.
 */
struct netif *NCM_LwipGetNetif(void);

/**
 * @brief Check if the physical / USB Ethernet link is currently active.
 * @return 1 if active, 0 if down.
 */
int NCM_LwipIsLinkUp(void);

#ifdef __cplusplus
}
#endif

#endif /* NCM_LWIP_H */
