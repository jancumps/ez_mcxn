# MCXN236 USB Ethernet (CDC-NCM) & USB Console Developer Guide

This project provides a bare-metal USB composite device for the **NXP MCXN236** (ARM Cortex-M33) microcontroller, featuring:

- **USB CDC-NCM Ethernet Adapter**: High-speed Ethernet supported out-of-the-box on Windows 10/11 (`UsbNcm.sys`), Linux (`cdc_ncm`), macOS, Android, and iOS without installing third-party drivers.
- **Zero-Configuration Embedded DHCP Server**: Automatically assigns `192.168.7.1` to the connected host computer or phone.
- **Isolated Local Link (No Gateway)**: Intentionally does **not** advertise a default gateway or DNS server. The host recognizes the device as a direct, private local link and continues using its existing Wi-Fi or LAN connection for internet access without interruption.
- **lwIP 2.2.1 TCP/IP Stack**: Non-blocking bare-metal networking (`NO_SYS=1`) supporting TCP, UDP, ICMP (Ping), and ARP.
- **USB CDC-ACM Serial Console**: Virtual COM port with standard `printf()` output redirection and an interactive command shell.

---

## Core Concepts

Before diving into code, read this!:

### 1. Protocol Control Blocks (PCB)

- **In plain language:** Think of a PCB as a **"Connection Handle"** or **"Socket File"** (similar to a socket descriptor in Python or standard POSIX C).
- In a desktop OS, when you call `s = socket.socket()`, the OS creates an internal structure to remember the IP address, port, and state of that socket. In lwIP, that data structure is called a **PCB**.
- **`struct tcp_pcb`**: A handle for a TCP connection. It tracks whether the connection is listening, connecting, or established, which remote IP and port it is communicating with, sequence numbers, and which callback functions to invoke when data arrives.
- **`struct udp_pcb`**: A handle for a UDP socket. It tracks the local port and the callback function to invoke when a UDP datagram arrives.
- **`struct pbuf`**: Stands for **Packet Buffer**. This is the memory structure that holds the raw bytes of network packets arriving from or being sent to the network.

---

### 2. UsbEthernet_Poll() Function

- **Overview:** This function moves data between the physical USB hardware controller and your software application.
- Because this microcontroller runs "bare metal" without an operating system, there are no background OS threads to automatically read USB hardware registers.
- When the host PC transmits data across the USB cable, the microcontroller's USB controller receives it into hardware FIFO memory.
- Calling **`UsbEthernet_Poll()`** checks the USB hardware, extracts incoming Ethernet frames and serial characters, feeds them into the lwIP network stack (which calls your `tcp_recv` or `udp_recv` callbacks), and transmits any packets waiting in the outbound queue back to the PC.
- **Suggested Execution Rate:** Call `UsbEthernet_Poll()` continuously in your `while (1)` main loop (i.e. as fast as the CPU loops). If no USB packets are pending, the function returns in just a few clock cycles with negligible overhead. Ensure other routines in your main loop do not block for longer than a few milliseconds (a polling frequency of **$\ge 1,000\text{ Hz}$ / at least once every 1 ms** is recommended for low latency).

---

### 3. Callback Execution

- lwIP is **callback-driven (event-driven)**. When you create a server (like the Simple TCP Server example below), you register a callback function (e.g. `my_tcp_recv`) and your setup function immediately returns.
- **`UsbEthernet_Poll()`** running in your `while (1)` loop is what actually triggers your callback. When a packet arrives from the PC, `UsbEthernet_Poll()` ingests the packet, and lwIP invokes your registered callback function synchronously during that poll call.
- The **`SysTick` timer** configured at 1 ms simply increments a counter (`s_milliseconds`). When `UsbEthernet_Poll()` runs, it checks `sys_now()` to determine if TCP timeouts or retransmissions are needed.

---

## Hardware & Network Configuration

| Parameter | Value | Details |
|---|---|---|
| **Microcontroller IP** | `192.168.7.2` | Static IPv4 assigned to lwIP `netif` |
| **Subnet Mask** | `255.255.255.0` | `/24` subnet (`192.168.7.0/24`) |
| **Host IP (Auto-Assigned)** | `192.168.7.1` | Leased automatically to the PC/phone via on-device DHCP |
| **Default Gateway** | *None* | Prevents host from redirecting internet traffic through USB |
| **Host MAC Address** | `02:00:00:00:00:01` | Advertised to host via USB CDC Ethernet descriptor |
| **Device MAC Address** | `02:00:00:00:00:02` | Used by microcontroller's lwIP Ethernet interface |
| **USB Controller** | High-Speed USB 2.0 (480 Mbps) | Composite CDC-ACM (Console) + CDC-NCM (Ethernet) |

---

## System Limitations & Concurrency

The networking stack is configured in [`usb_com_eth/lwipopts.h`](file:///C:/DEV/projects/mcx_projects/simple_usb_eth_project/usb_com_eth/lwipopts.h) with the following default resource limits:

| Parameter | Limit | Description |
|---|---|---|
| **Simultaneous Active TCP Connections** | **5 connections** | Maximum number of established, concurrent TCP client sockets |
| **Simultaneous TCP Listening Servers** | **4 servers** | Maximum number of different TCP ports listening at once |
| **Simultaneous UDP Sockets** | **4 sockets** | 1 is used by the embedded DHCP server, leaving 3 for user code |
| **TCP Maximum Segment Size (`TCP_MSS`)** | **1,460 bytes** | Standard Ethernet MTU (1500 bytes) minus 40 bytes IP/TCP header |
| **TCP Send Buffer (`TCP_SND_BUF`)** | **11,680 bytes** | Buffer space allocated for outbound in-flight data (`8 * TCP_MSS`) |
| **TCP Receive Window (`TCP_WND`)** | **11,680 bytes** | Inbound window advertised to the host (`8 * TCP_MSS`) |
| **Packet Buffer Pool (`PBUF_POOL_SIZE`)** | **16 buffers** | Pre-allocated 1,600-byte buffers for receiving incoming frames |
| **Fixed IP Subnet** | `192.168.7.0/24` | Point-to-point link: Host is always `192.168.7.1`, Device is `192.168.7.2` |

> [!NOTE]
> The MCXN236 microcontroller has **224 KB of contiguous SRAM**, and current firmware consumes only ~82 KB (36% utilization). If your application requires more simultaneous connections or larger buffers, simply increase `MEMP_NUM_TCP_PCB` or `PBUF_POOL_SIZE` in `usb_com_eth/lwipopts.h`.

---

## Step-by-Step Developer Guide: Minimal Examples

### 1. Minimal Application Template

Here is the complete template needed to run any network application on the board:

```c
#include <stdio.h>
#include "board.h"
#include "usb_com_eth.h"    /* UsbEthernet_Init(), UsbEthernet_Poll() */
#include "usb_console.h"    /* USB_ConsoleGetChar(), USB_ConsoleReady() */
#include "ncm_lwip.h"       /* NCM_LwipIsLinkUp() */
#include "lwip/tcp.h"       /* TCP functions & tcp_pcb */
#include "lwip/udp.h"       /* UDP functions & udp_pcb */
#include "lwip/ip4_addr.h"  /* IP4_ADDR() */

/* 1 ms SysTick timer for lwIP timeout management */
static volatile uint32_t s_milliseconds;
void SysTick_Handler(void) { ++s_milliseconds; }
uint32_t sys_now(void) { return s_milliseconds; }

int main(void)
{
    BOARD_InitHardware();

    /* 1. Configure 1 ms system tick timer */
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) { while (1) {} }

    /* 2. Initialize USB composite hardware, Ethernet (192.168.7.2), and DHCP server */
    UsbEthernet_Init();

    /* 3. Start your custom network services here (see examples below) */
    // Start_My_Tcp_Server(1234);

    while (1)
    {
        /* 4. MUST be called in the loop: moves USB packets and runs network timers */
        UsbEthernet_Poll();

        /* Your other non-blocking application tasks can run here */
    }
}
```

---

### 2. Simple TCP Server (Listen on a Port & Receive Data)

This example sets up a listening server on port `1234`, accepts connections from the PC, prints received text to the serial console, and sends a reply back:

```c
/* Callback: invoked by lwIP whenever incoming TCP data arrives from the client */
static err_t my_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (err != ERR_OK || p == NULL)
    {
        /* Client closed connection (p == NULL) or an error occurred */
        if (p) pbuf_free(p);
        tcp_close(tpcb);
        printf("[TCP] Client disconnected.\r\n");
        return ERR_OK;
    }

    /* Inform lwIP that we have consumed the bytes (advances receive window) */
    tcp_recved(tpcb, p->tot_len);

    /* Print received text (p->payload points to the raw bytes) */
    printf("[TCP] Received %u bytes: %.*s\r\n", p->tot_len, (int)p->len, (char *)p->payload);

    /* Send a reply back to the client */
    const char *reply = "Hello from MCXN236 TCP Server!\r\n";
    tcp_write(tpcb, reply, (u16_t)strlen(reply), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb); /* Flush immediately */

    /* Always free the pbuf when finished with it */
    pbuf_free(p);
    return ERR_OK;
}

/* Callback: invoked by lwIP when a new client connects */
static err_t my_tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;

    printf("[TCP] New client connected!\r\n");

    /* Register the receive callback for this new connection */
    tcp_recv(newpcb, my_tcp_recv);
    return ERR_OK;
}

/* Call this function once in main() before the while(1) loop */
void Start_My_Tcp_Server(uint16_t port)
{
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb && tcp_bind(pcb, IP_ADDR_ANY, port) == ERR_OK)
    {
        struct tcp_pcb *listen_pcb = tcp_listen(pcb);
        if (listen_pcb)
        {
            tcp_accept(listen_pcb, my_tcp_accept);
            printf("[TCP] Server listening on port %u\r\n", port);
        }
    }
}
```

**Testing with Python from your PC:**
```python
import socket

s = socket.create_connection(('192.168.7.2', 1234))
s.sendall(b"Hello Microcontroller!\n")
print(s.recv(1024).decode())
s.close()
```

---

### 3. Simple TCP Client (Connect to PC & Transmit Data)

This example initiates an outgoing connection from the microcontroller to a server running on your computer (`192.168.7.1:8080`) and sends a sensor reading:

```c
/* Callback: invoked when the TCP handshake with the remote host completes */
static err_t my_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    (void)arg;
    if (err == ERR_OK)
    {
        printf("[TCP Client] Connected to host server!\r\n");

        /* Send telemetry data */
        const char *data = "{\"sensor\": \"temperature\", \"value\": 24.5}\r\n";
        tcp_write(tpcb, data, (u16_t)strlen(data), TCP_WRITE_FLAG_COPY);
        tcp_output(tpcb);
    }
    else
    {
        printf("[TCP Client] Connection failed: %d\r\n", err);
        tcp_close(tpcb);
    }
    return ERR_OK;
}

void Send_Telemetry_To_Host(void)
{
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return;

    /* Target the PC's auto-assigned DHCP address (192.168.7.1) */
    ip4_addr_t host_ip;
    IP4_ADDR(&host_ip, 192, 168, 7, 1);

    printf("[TCP Client] Connecting to 192.168.7.1:8080...\r\n");
    tcp_connect(pcb, &host_ip, 8080, my_client_connected);
}
```

---

### 4. Simple UDP Server & Sender (Datagrams)

UDP is connectionless and has minimal latency, making it ideal for streaming sensor data or broadcasting announcements:

```c
/* Callback: invoked when a UDP packet arrives */
static void my_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    if (!p) return;

    printf("[UDP] Received %u bytes from %s:%u\r\n", p->tot_len, ipaddr_ntoa(addr), port);

    /* Echo the datagram directly back to the sender */
    udp_sendto(pcb, p, addr, port);

    /* Free packet buffer */
    pbuf_free(p);
}

void Start_My_Udp_Server(uint16_t port)
{
    struct udp_pcb *pcb = udp_new();
    if (pcb && udp_bind(pcb, IP_ADDR_ANY, port) == ERR_OK)
    {
        udp_recv(pcb, my_udp_recv, NULL);
        printf("[UDP] Server listening on port %u\r\n", port);
    }
}

/* Standalone function to transmit a UDP packet to the host */
void Send_Udp_To_Host(const uint8_t *payload, uint16_t length, uint16_t dest_port)
{
    struct udp_pcb *pcb = udp_new();
    if (!pcb) return;

    ip4_addr_t host_ip;
    IP4_ADDR(&host_ip, 192, 168, 7, 1);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
    if (p)
    {
        memcpy(p->payload, payload, length);
        udp_sendto(pcb, p, (const ip_addr_t *)&host_ip, dest_port);
        pbuf_free(p);
    }
    udp_remove(pcb);
}
```

---

### 5. USB Virtual COM Port (Serial Console & CLI)

The USB composite driver includes a full CDC-ACM serial port. Standard C library `printf()` is retargeted to this port:

```c
void Process_Serial_Terminal(void)
{
    int ch;
    /* Non-blocking read: returns -1 if no character is available */
    while ((ch = USB_ConsoleGetChar()) >= 0)
    {
        if (ch == 'p')
        {
            printf("Ping from console! Uptime: %lu s\r\n", sys_now() / 1000U);
        }
    }
}
```

---

## Intermediate & Advanced Patterns

### Full-Duplex TCP with Flow Control (Backpressure)

When transmitting large quantities of data (e.g. 64 KB transfers or high-speed streaming), you must observe TCP flow control. If the host PC sends faster than the board can process, or if the outbound TCP send buffer is momentarily full, calling `tcp_write()` blindly or dropping unwritten bytes will corrupt data or cause timeouts.

The pattern implemented in [`main.c`](file:///C:/DEV/projects/mcx_projects/simple_usb_eth_project/main.c#L24-L188) uses lwIP's official `tcpecho_raw` state machine:

1. **Queue Unsent Data**: When incoming data arrives, append it to a queue (`es->p = p` or `pbuf_cat(es->p, p)`).
2. **Partial Writes**: Check `u16_t sndbuf = tcp_sndbuf(tpcb)`. Write only up to `min(p->len, sndbuf)`.
3. **Chop Transmitted Data**: Call `pbuf_remove_header(p, to_write)` if a partial buffer was written, or free the pbuf if completed.
4. **Window Notification**: Call `tcp_recved(tpcb, to_write)` **only as data is accepted into the send buffer**. This slows down the host PC if the board's buffers are busy, guaranteeing zero packet drops.
5. **Draining via `tcp_sent`**: Register a `tcp_sent()` callback. Whenever the host acknowledges in-flight packets, lwIP calls `tcp_sent()`, which immediately drains the next chunk from the queue.

```c
struct echo_state {
    uint8_t state;
    struct tcp_pcb *pcb;
    struct pbuf *p; /* Queued data awaiting transmission */
};

static void echo_send(struct tcp_pcb *tpcb, struct echo_state *es)
{
    while (es->p != NULL)
    {
        u16_t sndbuf = tcp_sndbuf(tpcb);
        if (sndbuf == 0) break; /* Outbound TCP buffer full; wait for host ACK */

        u16_t to_write = es->p->len;
        if (to_write > sndbuf) to_write = sndbuf;

        err_t err = tcp_write(tpcb, es->p->payload, to_write, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) break;

        tcp_recved(tpcb, to_write); /* Advance receive window */

        if (to_write == es->p->len) {
            struct pbuf *next = es->p->next;
            if (next) pbuf_ref(next);
            pbuf_free(es->p);
            es->p = next;
        } else {
            pbuf_remove_header(es->p, to_write);
            break;
        }
    }
    tcp_output(tpcb);
}

/* Invoked by lwIP when remote host ACKs previously sent data */
static err_t echo_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    (void)len;
    struct echo_state *es = (struct echo_state *)arg;
    if (es && es->p) {
        echo_send(tpcb, es); /* Continue pumping queued data */
    }
    return ERR_OK;
}
```

---

## Complete API Reference

Functions are arranged in order of most likely to be used first. Plain-language descriptions precede technical details.

### Section 1: Lifecycle & System Functions

#### `UsbEthernet_Init()`
- **Description:** Sets up the USB controller, starts the virtual COM port, enables the Ethernet network interface (`192.168.7.2`), and starts the embedded DHCP server.
- **Signature:** `void UsbEthernet_Init(void)`
- **When to call:** Call once inside `main()` after `BOARD_InitHardware()` and `SysTick_Config()`.

#### `UsbEthernet_Poll()`
- **Description:** The main engine. Reads incoming packets from the USB hardware, gives them to lwIP, runs network timers, and transmits outbound data.
- **Signature:** `void UsbEthernet_Poll(void)`
- **When to call:** Call continuously inside your `while (1)` loop.

#### `NCM_LwipIsLinkUp()`
- **Description:** Checks if the USB cable is plugged in and the computer's network driver has activated the Ethernet link.
- **Signature:** `int NCM_LwipIsLinkUp(void)`
- **Returns:** `1` if the link is UP (active), `0` if DOWN (disconnected or idle).

#### `sys_now()`
- **Description:** Returns the system uptime in milliseconds.
- **Signature:** `uint32_t sys_now(void)`
- **Returns:** Elapsed time in milliseconds since boot (driven by `SysTick_Handler`).

---

### Section 2: TCP Server & Connection Functions

#### `tcp_new()` / `tcp_new_ip_type(type)`
- **Description:** Allocates a new, blank TCP connection handle (PCB).
- **Signature:** `struct tcp_pcb *tcp_new_ip_type(u8_t type)`
- **Parameters:** `IPADDR_TYPE_V4` for IPv4.
- **Returns:** Pointer to `struct tcp_pcb`, or `NULL` if out of memory.

#### `tcp_bind(pcb, ipaddr, port)`
- **Description:** Associates a TCP connection handle with a specific local port number (e.g. port 80 or 1234).
- **Signature:** `err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port)`
- **Parameters:** Pass `IP_ADDR_ANY` to listen on all local interfaces.
- **Returns:** `ERR_OK` on success, `ERR_USE` if port is already in use.

#### `tcp_listen(pcb)`
- **Description:** Puts the socket into listening mode so it waits for incoming connections from clients.
- **Signature:** `struct tcp_pcb *tcp_listen(struct tcp_pcb *pcb)`
- **Returns:** Pointer to a smaller listening PCB (the original `pcb` is freed).

#### `tcp_accept(pcb, accept_callback)`
- **Description:** Tells lwIP which function to call when a new client connects to your server.
- **Signature:** `void tcp_accept(struct tcp_pcb *pcb, tcp_accept_fn accept)`
- **Callback Signature:** `err_t accept(void *arg, struct tcp_pcb *newpcb, err_t err)`

#### `tcp_recv(pcb, recv_callback)`
- **Description:** Tells lwIP which function to call when incoming data arrives on a connection.
- **Signature:** `void tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn recv)`
- **Callback Signature:** `err_t recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)`
- **Important:** If `p == NULL`, the client has closed the connection.

#### `tcp_recved(pcb, len)`
- **Description:** Informs lwIP that your application has finished reading `len` bytes from the packet, telling lwIP to expand its advertised receive window so the remote PC can send more data.
- **Signature:** `void tcp_recved(struct tcp_pcb *pcb, u16_t len)`

#### `tcp_close(pcb)`
- **Description:** Cleanly closes a TCP connection with a standard FIN handshake.
- **Signature:** `err_t tcp_close(struct tcp_pcb *pcb)`

#### `tcp_abort(pcb)`
- **Description:** Instantly aborts a connection by sending a TCP RST (reset) packet and deallocates the PCB.
- **Signature:** `void tcp_abort(struct tcp_pcb *pcb)`

---

### Section 3: TCP Data Transmission

#### `tcp_write(pcb, data, len, flags)`
- **Description:** Places data into lwIP's outbound transmission queue.
- **Signature:** `err_t tcp_write(struct tcp_pcb *pcb, const void *dataptr, u16_t len, u8_t apiflags)`
- **Flags:** Use `TCP_WRITE_FLAG_COPY` to make lwIP copy the data into its internal buffers, allowing you to reuse your buffer immediately.
- **Returns:** `ERR_OK` on success, `ERR_MEM` if the send buffer is full.

#### `tcp_output(pcb)`
- **Description:** Forces lwIP to immediately send any data that was queued by `tcp_write()`, rather than waiting for Nagle's algorithm timer.
- **Signature:** `err_t tcp_output(struct tcp_pcb *pcb)`

#### `tcp_sndbuf(pcb)`
- **Description:** Checks how many bytes can currently be accepted by `tcp_write()` before the outbound buffer is full.
- **Signature:** `u16_t tcp_sndbuf(struct tcp_pcb *pcb)`
- **Returns:** Number of available bytes (up to `11,680` bytes).

#### `tcp_sent(pcb, sent_callback)`
- **Description:** Tells lwIP which function to call when the remote computer sends an ACK confirming it received data you previously transmitted.
- **Signature:** `void tcp_sent(struct tcp_pcb *pcb, tcp_sent_fn sent)`

---

### Section 4: TCP Client Functions

#### `tcp_connect(pcb, ipaddr, port, connected_callback)`
- **Description:** Initiates an outgoing connection from the microcontroller to a remote computer.
- **Signature:** `err_t tcp_connect(struct tcp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port, tcp_connected_fn connected)`

---

### Section 5: UDP Functions

#### `udp_new()`
- **Description:** Creates a new UDP socket handle.
- **Signature:** `struct udp_pcb *udp_new(void)`

#### `udp_bind(pcb, ipaddr, port)`
- **Description:** Binds a UDP socket handle to a local port.
- **Signature:** `err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port)`

#### `udp_recv(pcb, recv_callback, arg)`
- **Description:** Registers a callback function to run when a UDP datagram arrives on this port.
- **Signature:** `void udp_recv(struct udp_pcb *pcb, udp_recv_fn recv, void *recv_arg)`

#### `udp_sendto(pcb, p, dst_ip, dst_port)`
- **Description:** Transmits a UDP datagram directly to a destination IP and port.
- **Signature:** `err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *dst_ip, u16_t dst_port)`

#### `udp_remove(pcb)`
- **Description:** Deletes a UDP socket handle and frees its memory.
- **Signature:** `void udp_remove(struct udp_pcb *pcb)`

---

### Section 6: Packet Buffer (`pbuf`) Functions

#### `pbuf_free(p)`
- **Description:** Releases a packet buffer back to the memory pool.
- **Signature:** `u8_t pbuf_free(struct pbuf *p)`
- **Important:** In every `tcp_recv` or `udp_recv` callback, you **must** call `pbuf_free(p)` once you are done reading the data, otherwise memory will leak.

#### `pbuf_copy_partial(p, buffer, len, offset)`
- **Description:** Safely copies bytes from a packet buffer (even if the packet is split across a chain of buffers) into a standard C array.
- **Signature:** `u16_t pbuf_copy_partial(const struct pbuf *p, void *dataptr, u16_t len, u16_t offset)`

#### `pbuf_remove_header(p, len)`
- **Description:** Advances the start of the buffer by `len` bytes, effectively discarding the first `len` bytes from the front of the packet. Useful when partially writing data.
- **Signature:** `u8_t pbuf_remove_header(struct pbuf *p, size_t header_size_decrement)`

#### `pbuf_cat(head, tail)`
- **Description:** Chains two packet buffers together into a linked queue without copying memory.
- **Signature:** `void pbuf_cat(struct pbuf *h, struct pbuf *t)`

---

### Section 7: USB Serial Console Functions

#### `printf(...)`
- **Description:** Standard formatted printing. Outputs directly to the USB virtual COM port.

#### `USB_ConsoleGetChar()`
- **Description:** Reads one character from the USB virtual serial port.
- **Signature:** `int USB_ConsoleGetChar(void)`
- **Returns:** ASCII character value (`0` to `255`), or `-1` if no character is available.

#### `USB_ConsoleReady()`
- **Description:** Checks if a terminal program (e.g. PuTTY, MobaXterm) is currently connected and ready on the computer.
- **Signature:** `int USB_ConsoleReady(void)`
- **Returns:** `1` if terminal is open, `0` otherwise.

---

### Section 8: Key Data Structures (Plain Language Details)

#### `struct pbuf` (Packet Buffer)
```c
struct pbuf {
    struct pbuf *next;  /* Pointer to next pbuf if packet is chained */
    void *payload;      /* Pointer to the actual data bytes in RAM */
    u16_t tot_len;      /* Total length of this buffer plus all chained buffers */
    u16_t len;          /* Length of data bytes in this specific buffer */
};
```
- **How to use it:** Access incoming bytes at `(uint8_t *)p->payload`. Total byte count is `p->tot_len`.

#### `ip4_addr_t` (IPv4 Address)
```c
typedef struct ip4_addr {
    u32_t addr; /* 32-bit IPv4 address in network byte order */
} ip4_addr_t;
```
- **How to use it:** Use the macro `IP4_ADDR(&my_ip, 192, 168, 7, 1);` to initialize an address cleanly.

---

## Building, Flashing & Automated Verification

### Prerequisites

- **Toolchain**: Arm GNU Toolchain (`arm-none-eabi-gcc` in `PATH`).
- **Build System**: CMake 3.20+ and Ninja.
- **Flashing Utility**: NXP `blhost` in `PATH`.
- **lwIP**: In the project folder, type the following to install the lwIP source: 
```
git clone --depth 1 --branch STABLE-2_2_1_RELEASE https://github.com/lwip-tcpip/lwip.git lwip
``` 

### Build & Flash

```powershell
# 1. Compile firmware binary
.\build_project.ps1

# 2. Put board in ISP mode:
#    Hold ISP button -> Press and release Reset -> Release ISP button

# 3. Flash to board via USB
.\build_project.ps1 -Flash
```

### Verification & Automated Testing

Once flashed, connect the USB cable to your PC:
1. **Ping Test**:
   ```powershell
   ping 192.168.7.2
   ```
2. **Web Browser Status Dashboard**:
   Open [`http://192.168.7.2`](http://192.168.7.2) to view live connection metrics.
3. **Automated Test Suite**:
   Run the test script from PowerShell:
   ```powershell
   python .\test_tcp_echo.py
   ```
   This automatically verifies:
   - Short text echo (bidirectional RX/TX).
   - 64 KB full-duplex bulk streaming throughput.
   - Interactive command parsing (`stats`, `uptime`, `ping`).
   - Web server dashboard retrieval.
4. **Interactive USB Serial Console**:
   Open any serial terminal (e.g. PuTTY, MobaXterm, Tera Term) to the enumerated USB Serial COM Port at 115200 baud and type `status` or `help`.
