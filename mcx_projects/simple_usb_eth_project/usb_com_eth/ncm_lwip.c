/* General-purpose lwIP Ethernet interface for CDC-NCM USB Ethernet. */
#include <string.h>
#include <stdio.h>
#include "usb_device_config.h"
#include "usb_device.h"
#include "usb_device_descriptor.h"
#include "composite.h"
#include "ncm_device.h"
#include "ncm_lwip.h"
#include "usb_com_eth.h"
#include "dhcp_server.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include "lwip/err.h"

extern usb_device_composite_struct_t g_composite;
static struct netif s_netif;
static uint8_t s_linkWasUp;
static uint8_t s_frameOut[1514];

#if ETHARP_SUPPORT_STATIC_ENTRIES
static uint8_t s_hostMacLearned[6];
#endif

static err_t NCM_LinkOutput(struct netif *netif, struct pbuf *p)
{
    uint16_t n;
    (void)netif;
    if (p->tot_len > sizeof(s_frameOut)) return ERR_BUF;
    n = pbuf_copy_partial(p, s_frameOut, p->tot_len, 0);
    if (n != p->tot_len) return ERR_BUF;

    /* Pad to minimum Ethernet frame size (60 bytes without FCS).
     * Prevents host OS (Windows NDIS / Linux / macOS) from dropping
     * short frames (like ARP requests) as runts.
     */
    if (n < 60U)
    {
        memset(s_frameOut + n, 0, 60U - n);
        n = 60U;
    }

    if (NCM_DeviceSendFrame(g_composite.deviceHandle, s_frameOut, n))
    {
        return ERR_OK;
    }
    return ERR_MEM;
}

static err_t NCM_NetifInit(struct netif *netif)
{
    /* Device MAC address: 02:00:00:00:00:02.
     * Host MAC address assigned via CDC string descriptor 4 is 02:00:00:00:00:01.
     * Distinct MAC addresses are required so host OS (Windows/Linux/macOS) does not
     * drop incoming frames as loopback/reflections of its own interface.
     */
    static const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
    netif->name[0] = 'u';
    netif->name[1] = 'n';
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, mac, 6);
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    netif->output = etharp_output;
    netif->linkoutput = NCM_LinkOutput;
    return ERR_OK;
}

void NCM_LwipInit(void)
{
    ip4_addr_t ip, mask, gateway, client_ip;
    lwip_init();

    /* Device IP: 192.168.7.2 / 24. No default gateway. */
    IP4_ADDR(&ip, 192, 168, 7, 2);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 0, 0, 0, 0);

    netif_add(&s_netif, &ip, &mask, &gateway, NULL, NCM_NetifInit, ethernet_input);
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);

    /* Start embedded DHCP server to automatically assign 192.168.7.1 to connected host */
    IP4_ADDR(&client_ip, 192, 168, 7, 1);
    DHCPServer_Start(&s_netif, &client_ip);
}

void NCM_LwipReceive(const uint8_t *frame, uint16_t length)
{
    struct pbuf *p;
    if (!s_linkWasUp || length < 14) return;

#if ETHARP_SUPPORT_STATIC_ENTRIES
    /* Opportunistically learn the host MAC address from any incoming IPv4 or ARP packet
     * and lock it as a static ARP entry. This eliminates ARP expiration during streaming.
     */
    if (length >= 42U && frame[12] == 0x08U)
    {
        const uint8_t *src_ip = NULL;
        const uint8_t *src_mac = frame + 6;
        if (frame[13] == 0x00U && length >= 34U)
        {
            src_ip = frame + 26; /* IPv4 source IP */
        }
        else if (frame[13] == 0x06U)
        {
            src_ip = frame + 28; /* ARP sender IP */
            src_mac = frame + 22; /* ARP sender MAC */
        }
        if (src_ip && src_ip[0] == 192U && src_ip[1] == 168U && src_ip[2] == 7U && src_ip[3] == 1U)
        {
            if (memcmp(s_hostMacLearned, src_mac, 6) != 0)
            {
                ip4_addr_t host_ip;
                struct eth_addr host_mac;
                memcpy(s_hostMacLearned, src_mac, 6);
                IP4_ADDR(&host_ip, 192, 168, 7, 1);
                memcpy(host_mac.addr, src_mac, 6);
                etharp_add_static_entry(&host_ip, &host_mac);
            }
        }
    }
#endif

    p = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
    if (!p) return;
    if (pbuf_take(p, frame, length) != ERR_OK)
    {
        pbuf_free(p);
        return;
    }
    if (s_netif.input(p, &s_netif) != ERR_OK)
    {
        pbuf_free(p);
    }
}

void NCM_LwipPoll(void)
{
    uint8_t up = (uint8_t)NCM_DeviceLinkActive();
    if (up != s_linkWasUp)
    {
        s_linkWasUp = up;
        if (up)
        {
            netif_set_link_up(&s_netif);
            ip4_addr_t client_ip;
            IP4_ADDR(&client_ip, 192, 168, 7, 1);
            DHCPServer_Start(&s_netif, &client_ip);
        }
        else
        {
            netif_set_link_down(&s_netif);
            DHCPServer_Stop();
#if ETHARP_SUPPORT_STATIC_ENTRIES
            memset(s_hostMacLearned, 0, sizeof(s_hostMacLearned));
#endif
        }
    }
    sys_check_timeouts();
}

struct netif *NCM_LwipGetNetif(void)
{
    return &s_netif;
}

int NCM_LwipIsLinkUp(void)
{
    return s_linkWasUp != 0;
}

void UsbEthernet_Init(void)
{
    APPInit();
    NCM_LwipInit();
}

void UsbEthernet_Poll(void)
{
    DualUsbCom_Poll();
    NCM_LwipPoll();
}

