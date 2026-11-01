#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "usb_com_eth.h"
#include "usb_console.h"
#include "ncm_device.h"
#include "ncm_lwip.h"
#include "dhcp_server.h"
#include "lwip/tcp.h"
#include "lwip/ip4_addr.h"
#include "fsl_device_registers.h"

static volatile uint32_t s_milliseconds;
void SysTick_Handler(void) { ++s_milliseconds; }
uint32_t sys_now(void) { return s_milliseconds; }

/* Global demo statistics */
static uint32_t g_echoRxBytes;
static uint32_t g_echoTxBytes;
static uint32_t g_echoConnections;

/* -------------------------------------------------------------------------- */
/* TCP Echo Server (Port 5000) with True Backpressure & Flow Control          */
/* -------------------------------------------------------------------------- */

enum echo_states
{
    ES_NONE = 0,
    ES_ACCEPTED,
    ES_RECEIVED,
    ES_CLOSING
};

struct echo_state
{
    uint8_t state;
    struct tcp_pcb *pcb;
    struct pbuf *p; /* Queued incoming pbuf chain awaiting transmission */
};

static void echo_close(struct tcp_pcb *tpcb, struct echo_state *es)
{
    tcp_arg(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    tcp_recv(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_poll(tpcb, NULL, 0);

    if (es != NULL)
    {
        if (es->p != NULL)
        {
            pbuf_free(es->p);
        }
        mem_free(es);
    }
    tcp_close(tpcb);
}

static void echo_send(struct tcp_pcb *tpcb, struct echo_state *es)
{
    while (es->p != NULL)
    {
        u16_t sndbuf = tcp_sndbuf(tpcb);
        if (sndbuf == 0) break;

        u16_t to_write = es->p->len;
        if (to_write > sndbuf) to_write = sndbuf;

        err_t err = tcp_write(tpcb, es->p->payload, to_write, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) break;

        g_echoTxBytes += to_write;
        tcp_recved(tpcb, to_write);

        if (to_write == es->p->len)
        {
            struct pbuf *next = es->p->next;
            if (next != NULL) pbuf_ref(next);
            pbuf_free(es->p);
            es->p = next;
        }
        else
        {
            pbuf_remove_header(es->p, to_write);
            break;
        }
    }
    tcp_output(tpcb);
}

static err_t echo_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    (void)len;
    struct echo_state *es = (struct echo_state *)arg;
    if (es != NULL)
    {
        if (es->p != NULL)
        {
            echo_send(tpcb, es);
        }
        else if (es->state == ES_CLOSING)
        {
            echo_close(tpcb, es);
        }
    }
    return ERR_OK;
}

static err_t echo_poll(void *arg, struct tcp_pcb *tpcb)
{
    struct echo_state *es = (struct echo_state *)arg;
    if (es != NULL)
    {
        if (es->p != NULL)
        {
            echo_send(tpcb, es);
        }
        else if (es->state == ES_CLOSING)
        {
            echo_close(tpcb, es);
        }
    }
    return ERR_OK;
}

static void echo_err(void *arg, err_t err)
{
    (void)err;
    struct echo_state *es = (struct echo_state *)arg;
    if (es != NULL)
    {
        if (es->p != NULL)
        {
            pbuf_free(es->p);
        }
        mem_free(es);
    }
}

static err_t echo_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct echo_state *es = (struct echo_state *)arg;
    if (err != ERR_OK || es == NULL)
    {
        if (p) pbuf_free(p);
        return ERR_VAL;
    }

    if (p == NULL)
    {
        /* Remote client closed connection */
        es->state = ES_CLOSING;
        if (es->p == NULL)
        {
            echo_close(tpcb, es);
        }
        else
        {
            echo_send(tpcb, es);
        }
        return ERR_OK;
    }

    g_echoRxBytes += p->tot_len;

    /* Interactive commands for single-line telnet/netcat sessions */
    if (es->state == ES_ACCEPTED && p->len < 64U)
    {
        char cmd[64];
        uint16_t clen = p->len;
        if (clen >= sizeof(cmd)) clen = sizeof(cmd) - 1U;
        memcpy(cmd, p->payload, clen);
        cmd[clen] = '\0';
        while (clen > 0 && (cmd[clen - 1] == '\r' || cmd[clen - 1] == '\n' || cmd[clen - 1] == ' '))
        {
            cmd[--clen] = '\0';
        }

        if (strcmp(cmd, "help") == 0)
        {
            const char *msg = "MCXN236 Echo Server. Commands: help, stats, uptime, ping\r\nOr send any data to echo.\r\n";
            u16_t mlen = (u16_t)strlen(msg);
            tcp_write(tpcb, msg, mlen, TCP_WRITE_FLAG_COPY);
            tcp_output(tpcb);
            g_echoTxBytes += mlen;
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
            return ERR_OK;
        }
        else if (strcmp(cmd, "stats") == 0)
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "Uptime: %lu s | RX: %lu B | TX: %lu B\r\n",
                     (unsigned long)(sys_now() / 1000U),
                     (unsigned long)g_echoRxBytes, (unsigned long)g_echoTxBytes);
            u16_t mlen = (u16_t)strlen(msg);
            tcp_write(tpcb, msg, mlen, TCP_WRITE_FLAG_COPY);
            tcp_output(tpcb);
            g_echoTxBytes += mlen;
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
            return ERR_OK;
        }
        else if (strcmp(cmd, "uptime") == 0)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "Uptime: %lu seconds\r\n", (unsigned long)(sys_now() / 1000U));
            u16_t mlen = (u16_t)strlen(msg);
            tcp_write(tpcb, msg, mlen, TCP_WRITE_FLAG_COPY);
            tcp_output(tpcb);
            g_echoTxBytes += mlen;
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
            return ERR_OK;
        }
        else if (strcmp(cmd, "ping") == 0)
        {
            const char *msg = "pong\r\n";
            u16_t mlen = (u16_t)strlen(msg);
            tcp_write(tpcb, msg, mlen, TCP_WRITE_FLAG_COPY);
            tcp_output(tpcb);
            g_echoTxBytes += mlen;
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
            return ERR_OK;
        }
    }

    es->state = ES_RECEIVED;

    /* Append to chain of pending data */
    if (es->p == NULL)
    {
        es->p = p;
    }
    else
    {
        pbuf_cat(es->p, p);
    }

    echo_send(tpcb, es);
    return ERR_OK;
}

static err_t echo_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;

    struct echo_state *es = (struct echo_state *)mem_malloc(sizeof(struct echo_state));
    if (!es) return ERR_MEM;

    es->state = ES_ACCEPTED;
    es->pcb = newpcb;
    es->p = NULL;

    tcp_arg(newpcb, es);
    tcp_recv(newpcb, echo_recv);
    tcp_sent(newpcb, echo_sent);
    tcp_err(newpcb, echo_err);
    tcp_poll(newpcb, echo_poll, 1);

    ++g_echoConnections;
    return ERR_OK;
}

static void Demo_EchoServerInit(uint16_t port)
{
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb && tcp_bind(pcb, IP_ANY_TYPE, port) == ERR_OK)
    {
        struct tcp_pcb *l = tcp_listen(pcb);
        if (l) tcp_accept(l, echo_accept);
    }
}

/* -------------------------------------------------------------------------- */
/* HTTP Status Server (Port 80)                                               */
/* -------------------------------------------------------------------------- */

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (err != ERR_OK || p == NULL)
    {
        if (p) pbuf_free(p);
        tcp_close(tpcb);
        return ERR_OK;
    }

    tcp_recved(tpcb, p->tot_len);

    if (p->len >= 3 && memcmp(p->payload, "GET", 3) == 0)
    {
        static char body[1024];
        static char resp[1200];
        uint32_t uptime = sys_now() / 1000U;
        int body_len = snprintf(body, sizeof(body),
            "<!DOCTYPE html><html><head><title>MCXN236</title>"
            "<style>body{font-family:sans-serif;margin:40px;background:#f5f6fa;color:#333}"
            ".card{background:#fff;padding:24px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.1);max-width:550px}"
            "h2{color:#2e86de;margin-top:0}table{width:100%%;border-collapse:collapse;margin-top:16px}"
            "td,th{padding:8px;border-bottom:1px solid #eee;text-align:left}.ok{color:#27ae60;font-weight:bold}</style></head>"
            "<body><div class=\"card\"><h2>MCXN236 USB Ethernet</h2>"
            "<p class=\"ok\">&#x25CF; Link Connected (CDC-NCM)</p>"
            "<table>"
            "<tr><th>Device IP</th><td>192.168.7.2</td></tr>"
            "<tr><th>Host IP (DHCP)</th><td>192.168.7.1</td></tr>"
            "<tr><th>Subnet Mask</th><td>255.255.255.0</td></tr>"
            "<tr><th>Gateway</th><td>None (Isolated Local Link)</td></tr>"
            "<tr><th>Uptime</th><td>%lu s</td></tr>"
            "<tr><th>TCP Echo Server</th><td>Port 5000 (Active)</td></tr>"
            "<tr><th>Echo RX / TX</th><td>%lu / %lu B</td></tr>"
            "</table></div></body></html>",
            (unsigned long)uptime,
            (unsigned long)g_echoRxBytes, (unsigned long)g_echoTxBytes);

        if (body_len > (int)sizeof(body) - 1) body_len = (int)sizeof(body) - 1;

        int resp_len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n%s",
            body_len, body);

        if (resp_len > (int)sizeof(resp) - 1) resp_len = (int)sizeof(resp) - 1;

        tcp_write(tpcb, resp, (u16_t)resp_len, TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
    }

    pbuf_free(p);
    tcp_close(tpcb);
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void Demo_HttpServerInit(uint16_t port)
{
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb && tcp_bind(pcb, IP_ANY_TYPE, port) == ERR_OK)
    {
        struct tcp_pcb *l = tcp_listen(pcb);
        if (l) tcp_accept(l, http_accept);
    }
}

/* -------------------------------------------------------------------------- */
/* USB CDC-ACM Serial Console Command Processor                               */
/* -------------------------------------------------------------------------- */

static void ProcessConsoleCommand(const char *cmd)
{
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    if (strcmp(cmd, "help") == 0)
    {
        printf("\r\nAvailable Commands:\r\n");
        printf("  help     - Show this help menu\r\n");
        printf("  status   - Show network and demo statistics\r\n");
        printf("  ip       - Show IP address and link state\r\n");
        printf("  dhcp     - Show DHCP lease information\r\n");
        printf("  uptime   - Show system uptime\r\n");
    }
    else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "stats") == 0)
    {
        uint8_t mac[6];
        ncm_transport_diag_t diag;
        NCM_DeviceGetDiagnostics(&diag);
        printf("\r\n--- System & Network Status ---\r\n");
        printf("Link State:       %s\r\n", NCM_LwipIsLinkUp() ? "UP (Connected)" : "DOWN");
        printf("Device IP:        192.168.7.2 / 255.255.255.0\r\n");
        printf("DHCP Server:      Active (Pool: 192.168.7.1, No Gateway)\r\n");
        if (DHCPServer_GetClientMac(mac))
        {
            printf("DHCP Client:      192.168.7.1 [%02X:%02X:%02X:%02X:%02X:%02X]\r\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        else
        {
            printf("DHCP Client:      Waiting for request\r\n");
        }
        printf("USB NCM Frames:   RX: %lu, TX: %lu\r\n",
               (unsigned long)diag.rxFrames, (unsigned long)diag.txFrames);
        printf("TCP Echo Port:    5000 (RX: %lu B, TX: %lu B, Connections: %lu)\r\n",
               (unsigned long)g_echoRxBytes, (unsigned long)g_echoTxBytes, (unsigned long)g_echoConnections);
        printf("HTTP Web Port:    80\r\n");
        printf("Uptime:           %lu seconds\r\n", (unsigned long)(sys_now() / 1000U));
    }
    else if (strcmp(cmd, "ip") == 0)
    {
        printf("\r\nDevice IP:  192.168.7.2\r\n");
        printf("Netmask:    255.255.255.0\r\n");
        printf("Gateway:    None (local link only)\r\n");
        printf("Link:       %s\r\n", NCM_LwipIsLinkUp() ? "UP" : "DOWN");
    }
    else if (strcmp(cmd, "dhcp") == 0)
    {
        uint8_t mac[6];
        if (DHCPServer_GetClientMac(mac))
        {
            printf("\r\nDHCP Leased: 192.168.7.1 -> %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        else
        {
            printf("\r\nDHCP Server active. No active lease yet.\r\n");
        }
    }
    else if (strcmp(cmd, "uptime") == 0)
    {
        printf("\r\nUptime: %lu seconds\r\n", (unsigned long)(sys_now() / 1000U));
    }
    else
    {
        printf("\r\nUnknown command: '%s'. Type 'help' for command list.\r\n", cmd);
    }
    printf("> ");
}

/* -------------------------------------------------------------------------- */
/* Main Application                                                           */
/* -------------------------------------------------------------------------- */

int main(void)
{
    BOARD_InitHardware();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) { while (1) {} }

    /* Initialize USB Controller, Ethernet (192.168.7.2), and DHCP Server */
    UsbEthernet_Init();

    /* Start Demo Services */
    Demo_EchoServerInit(5000); /* Bidirectional TCP Echo Server */
    Demo_HttpServerInit(80);   /* HTTP Status Web Server */

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    uint8_t wasReady = 0U;
    char lineBuf[64];
    uint8_t linePos = 0;

    while (1)
    {
        /* Process USB packet transfers and execute lwIP timers */
        UsbEthernet_Poll();

        uint8_t ready = (uint8_t)USB_ConsoleReady();

        if (ready && !wasReady)
        {
            clearerr(stdout);
            clearerr(stderr);

            printf("\r\n========================================\r\n");
            printf(" MCXN236 USB Ethernet (CDC-NCM) Demo\r\n");
            printf("========================================\r\n");
            printf("IP Address:   192.168.7.2 (No gateway)\r\n");
            printf("DHCP Server:  192.168.7.1 auto-assigned\r\n");
            printf("TCP Echo:     Port 5000\r\n");
            printf("HTTP Server:  Port 80 (http://192.168.7.2)\r\n");
            printf("Type 'help' for commands.\r\n> ");
            linePos = 0;
        }

        wasReady = ready;

        int ch;
        while ((ch = USB_ConsoleGetChar()) >= 0)
        {
            if (ch == '\r' || ch == '\n')
            {
                lineBuf[linePos] = '\0';
                ProcessConsoleCommand(lineBuf);
                linePos = 0;
            }
            else if (ch == '\b' || ch == 0x7F)
            {
                if (linePos > 0)
                {
                    --linePos;
                    printf("\b \b");
                }
            }
            else if (linePos < sizeof(lineBuf) - 1U && ch >= 32 && ch <= 126)
            {
                lineBuf[linePos++] = (char)ch;
                putchar(ch);
            }
        }
    }
}
