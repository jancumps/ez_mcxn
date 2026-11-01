#include "dhcp_server.h"
#include <string.h>
#include "lwip/udp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/prot/iana.h"
#include "lwip/etharp.h"

#ifndef LWIP_IANA_PORT_DHCP_SERVER
#define LWIP_IANA_PORT_DHCP_SERVER 67
#endif
#ifndef LWIP_IANA_PORT_DHCP_CLIENT
#define LWIP_IANA_PORT_DHCP_CLIENT 68
#endif

static struct udp_pcb *s_pcb;
static struct netif *s_netif;
static ip4_addr_t s_client_ip;
static uint8_t s_client_mac[6];
static int s_leased;

/* Static buffers to prevent stack exhaustion on Cortex-M bare-metal systems */
static uint8_t s_req_buf[576];
static uint8_t s_reply_buf[350];

static void dhcp_server_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                             const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)addr;
    (void)port;

    if (!p || !s_netif)
    {
        if (p) pbuf_free(p);
        return;
    }

    uint16_t req_len = pbuf_copy_partial(p, s_req_buf, sizeof(s_req_buf), 0);
    pbuf_free(p);

    if (req_len < DHCP_MSG_LEN + 4U) return;

    const struct dhcp_msg *req = (const struct dhcp_msg *)s_req_buf;
    if (req->op != DHCP_BOOTREQUEST || req->htype != 1U || req->hlen != 6U) return;

    /* Verify DHCP magic cookie: 0x63, 0x82, 0x53, 0x63 */
    if (s_req_buf[DHCP_MSG_LEN]     != 0x63U ||
        s_req_buf[DHCP_MSG_LEN + 1] != 0x82U ||
        s_req_buf[DHCP_MSG_LEN + 2] != 0x53U ||
        s_req_buf[DHCP_MSG_LEN + 3] != 0x63U)
    {
        return;
    }

    /* Parse DHCP options */
    uint8_t msg_type = 0;
    ip4_addr_t req_ip = {0};
    ip4_addr_t req_server_id = {0};
    int has_req_ip = 0;
    int has_server_id = 0;

    uint16_t opt_idx = DHCP_OPTIONS_OFS;
    while (opt_idx < req_len)
    {
        uint8_t opt = s_req_buf[opt_idx++];
        if (opt == DHCP_OPTION_END) break;
        if (opt == DHCP_OPTION_PAD) continue;
        if (opt_idx >= req_len) break;
        uint8_t opt_len = s_req_buf[opt_idx++];
        if (opt_idx + opt_len > req_len) break;

        if (opt == DHCP_OPTION_MESSAGE_TYPE && opt_len == 1U)
        {
            msg_type = s_req_buf[opt_idx];
        }
        else if (opt == DHCP_OPTION_REQUESTED_IP && opt_len == 4U)
        {
            memcpy(&req_ip, s_req_buf + opt_idx, 4);
            has_req_ip = 1;
        }
        else if (opt == DHCP_OPTION_SERVER_ID && opt_len == 4U)
        {
            memcpy(&req_server_id, s_req_buf + opt_idx, 4);
            has_server_id = 1;
        }
        opt_idx += opt_len;
    }

    if (msg_type != DHCP_DISCOVER && msg_type != DHCP_REQUEST && msg_type != DHCP_INFORM)
    {
        return;
    }

    /* If client specified a Server Identifier that is not us, ignore it */
    if (has_server_id && !ip4_addr_eq(&req_server_id, netif_ip4_addr(s_netif)))
    {
        return;
    }

    /* Check if client requested an IP from an incompatible subnet */
    int send_nak = 0;
    if (msg_type == DHCP_REQUEST && has_req_ip)
    {
        if (!ip4_addr_net_eq(&req_ip, netif_ip4_addr(s_netif), netif_ip4_netmask(s_netif)))
        {
            send_nak = 1;
        }
    }

    memset(s_reply_buf, 0, sizeof(s_reply_buf));
    struct dhcp_msg *reply = (struct dhcp_msg *)s_reply_buf;
    reply->op = DHCP_BOOTREPLY;
    reply->htype = req->htype;
    reply->hlen = req->hlen;
    reply->hops = 0;
    reply->xid = req->xid;
    reply->secs = 0;
    reply->flags = req->flags;

    /* siaddr is 0.0.0.0 per RFC 2131 Section 4.3.1 (no TFTP server) */
    reply->giaddr = req->giaddr;
    memcpy(reply->chaddr, req->chaddr, 16);

    /* Magic cookie */
    s_reply_buf[DHCP_MSG_LEN]     = 0x63U;
    s_reply_buf[DHCP_MSG_LEN + 1] = 0x82U;
    s_reply_buf[DHCP_MSG_LEN + 2] = 0x53U;
    s_reply_buf[DHCP_MSG_LEN + 3] = 0x63U;

    uint8_t *opt = s_reply_buf + DHCP_OPTIONS_OFS;

    if (send_nak)
    {
        *opt++ = DHCP_OPTION_MESSAGE_TYPE;
        *opt++ = 1;
        *opt++ = DHCP_NAK;

        *opt++ = DHCP_OPTION_SERVER_ID;
        *opt++ = 4;
        memcpy(opt, netif_ip4_addr(s_netif), 4);
        opt += 4;

        *opt++ = DHCP_OPTION_END;
    }
    else
    {
        if (msg_type != DHCP_INFORM)
        {
            memcpy(&reply->yiaddr, &s_client_ip, 4);
        }

        /* Option 53: Message Type (OFFER or ACK) */
        *opt++ = DHCP_OPTION_MESSAGE_TYPE;
        *opt++ = 1;
        *opt++ = (msg_type == DHCP_DISCOVER) ? DHCP_OFFER : DHCP_ACK;

        /* Option 54: Server Identifier */
        *opt++ = DHCP_OPTION_SERVER_ID;
        *opt++ = 4;
        memcpy(opt, netif_ip4_addr(s_netif), 4);
        opt += 4;

        /* Option 1: Subnet Mask */
        *opt++ = DHCP_OPTION_SUBNET_MASK;
        *opt++ = 4;
        memcpy(opt, netif_ip4_netmask(s_netif), 4);
        opt += 4;

        if (msg_type != DHCP_INFORM)
        {
            /* Option 51: Lease Time (86400 seconds = 24 hours) */
            *opt++ = DHCP_OPTION_LEASE_TIME;
            *opt++ = 4;
            uint32_t lease = PP_HTONL(86400UL);
            memcpy(opt, &lease, 4);
            opt += 4;

            /* Option 58: Renewal Time T1 (43200 seconds = 12 hours) */
            *opt++ = 58;
            *opt++ = 4;
            uint32_t t1 = PP_HTONL(43200UL);
            memcpy(opt, &t1, 4);
            opt += 4;

            /* Option 59: Rebinding Time T2 (75600 seconds = 21 hours) */
            *opt++ = 59;
            *opt++ = 4;
            uint32_t t2 = PP_HTONL(75600UL);
            memcpy(opt, &t2, 4);
            opt += 4;
        }

        /* Option 28: Broadcast Address */
        *opt++ = 28;
        *opt++ = 4;
        ip4_addr_t bcast;
        IP4_ADDR(&bcast, ip4_addr1(&s_client_ip), ip4_addr2(&s_client_ip), ip4_addr3(&s_client_ip), 255);
        memcpy(opt, &bcast, 4);
        opt += 4;

        /* NOTE: Intentionally NO Option 3 (Router/Gateway) and NO Option 6 (DNS).
         * This instructs the host OS (Windows/Linux/macOS/Android/iOS) that this link
         * has no internet access, preventing the host from redirecting default route traffic here.
         */

        /* Option 255: End */
        *opt++ = DHCP_OPTION_END;

        if (msg_type == DHCP_REQUEST)
        {
            s_leased = 1;
            memcpy(s_client_mac, req->chaddr, 6);
#if ETHARP_SUPPORT_STATIC_ENTRIES
            etharp_add_static_entry(&s_client_ip, (struct eth_addr *)s_client_mac);
#endif
        }
    }

    uint16_t reply_len = (uint16_t)(opt - s_reply_buf);
    /* RFC 2131 Section 2: minimum BOOTP vendor options field is 64 octets,
     * so minimum total BOOTP message length is 236 + 4 + 68 = 308 octets.
     */
    if (reply_len < 308U) reply_len = 308U;

    struct pbuf *p_resp = pbuf_alloc(PBUF_TRANSPORT, reply_len, PBUF_RAM);
    if (p_resp)
    {
        memcpy(p_resp->payload, s_reply_buf, reply_len);
        udp_sendto_if(pcb, p_resp, IP_ADDR_BROADCAST, LWIP_IANA_PORT_DHCP_CLIENT, s_netif);
        pbuf_free(p_resp);
    }
}

err_t DHCPServer_Start(struct netif *netif, const ip4_addr_t *client_ip)
{
    if (!netif || !client_ip) return ERR_ARG;
    if (s_pcb) DHCPServer_Stop();

    s_netif = netif;
    s_client_ip = *client_ip;
    s_leased = 0;

    s_pcb = udp_new();
    if (!s_pcb) return ERR_MEM;

    err_t err = udp_bind(s_pcb, IP_ADDR_ANY, LWIP_IANA_PORT_DHCP_SERVER);
    if (err != ERR_OK)
    {
        udp_remove(s_pcb);
        s_pcb = NULL;
        return err;
    }

    udp_recv(s_pcb, dhcp_server_recv, NULL);
    return ERR_OK;
}

void DHCPServer_Stop(void)
{
    if (s_pcb)
    {
        udp_remove(s_pcb);
        s_pcb = NULL;
    }
    s_leased = 0;
}

int DHCPServer_IsLeased(void)
{
    return s_leased;
}

int DHCPServer_GetClientMac(uint8_t mac[6])
{
    if (!s_leased || !mac) return 0;
    memcpy(mac, s_client_mac, 6);
    return 1;
}
