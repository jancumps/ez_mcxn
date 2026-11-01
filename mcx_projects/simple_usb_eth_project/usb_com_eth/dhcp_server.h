#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the embedded DHCP server on the given network interface.
 *
 * Hands out a lease for client_ip (e.g. 192.168.7.1) with subnet mask 255.255.255.0.
 * Intentionally does NOT advertise a default gateway (Option 3) or DNS (Option 6),
 * ensuring the attached host OS (Windows, Linux, macOS, iOS, Android) will not
 * route internet traffic through this USB Ethernet link.
 *
 * @param netif The network interface to bind to.
 * @param client_ip The IPv4 address to offer to the connected host.
 * @return ERR_OK on success, or an lwIP error code.
 */
err_t DHCPServer_Start(struct netif *netif, const ip4_addr_t *client_ip);

/**
 * @brief Stop the embedded DHCP server and free resources.
 */
void DHCPServer_Stop(void);

/**
 * @brief Check if a client has been granted a lease.
 * @return 1 if leased, 0 otherwise.
 */
int DHCPServer_IsLeased(void);

/**
 * @brief Get the MAC address of the leased client.
 * @param mac Buffer of at least 6 bytes.
 * @return 1 if MAC is available, 0 otherwise.
 */
int DHCPServer_GetClientMac(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* DHCP_SERVER_H */
