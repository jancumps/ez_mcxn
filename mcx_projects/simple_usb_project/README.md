# simple_usb_project

Bare-metal NXP MCXN236 (`ez_mcxn`) demonstration of **two independent USB CDC ACM virtual COM ports**, built with standalone CMake, Ninja and Arm GNU Toolchain. No West or Kconfig is needed for the standalone build.

## Project layout

```text
simple_usb_project/
├── CMakeLists.txt              # Application and firmware link settings
├── build_project.ps1           # Build / flash helper (local project name)
├── main.c                      # CDC0 console demo and CDC1 echo demo
├── syscalls.c                  # Bare-metal Newlib stubs
└── dual_usb_com/
    ├── CMakeLists.txt          # Reusable USB static library
    ├── dual_usb_com.h          # Initialization and polling API
    ├── usb_console.h           # CDC0 and CDC1 public APIs
    ├── retarget_stdio.c        # _write() to route printf to CDC0
    ├── virtual_com.c           # USB CDC data handling and FIFOs
    └── ...                     # NXP USB stack, board support, config
```

## Build and flash

From PowerShell in the project root:

```powershell
.\build_project.ps1 -clean
.\build_project.ps1 -flash
```

The firmware binary is generated at `build-standalone/simple_usb_project.bin`. To flash, put the board into USB ISP/bootloader mode.

## USB ports at a glance

| USB CDC instance | Windows COM port | Current demo behavior | API |
|---|---|---|---|
| CDC0 | Assigned by Windows | `printf()` console; received characters reported as hex | `USB_Console*` |
| CDC1 | Assigned by Windows | Echoes received bytes | `USB_Port1*` |

Windows assigns COM numbers dynamically. **Do not assume CDC0 is always the lower COM number**. Identify them by testing: the echo port is CDC1; the port that prints `MCXN236 USB console ready` and `RX: 0x..` is CDC0. A terminal may need to assert DTR for a CDC port to become ready. The baud-rate setting in a host terminal is not a physical UART bit rate; both ports are USB virtual serial interfaces.

## Initialization and servicing

Include the public headers:

```c
#include "dual_usb_com.h"
#include "usb_console.h"
```

Initialize after board hardware setup and service USB continuously:

```c
BOARD_InitHardware();
APPInit();

while (1)
{
    DualUsbCom_Poll();
    /* Application work for either port */
}
```

The public API is intended for **main-loop context**, not interrupt handlers. Calls are nonblocking; do not assume a write always accepts all requested bytes.

## CDC1: using the second COM port

CDC1 is no longer hardcoded to echo inside the library. The application controls what to do with received data and what to transmit.

Declared in `dual_usb_com/usb_console.h`:

```c
int USB_Port1Ready(void);
int USB_Port1GetChar(void);
int USB_Port1Write(const uint8_t *data, unsigned int length);
```

- `USB_Port1Ready()` is nonzero when CDC1 is ready for application traffic.
- `USB_Port1GetChar()` returns one received byte (`0`–`255`), or `-1` when the receive FIFO is empty.
- `USB_Port1Write(data, length)` queues data for transmission and returns the **number of bytes accepted**, which can be smaller than `length` or zero if the transmit FIFO is full.

### Example: echo CDC1 in `main.c`

This is the pattern used in the working demonstration. It retains a byte until the transmit FIFO accepts it:

```c
int pendingEcho = -1;

while (1)
{
    DualUsbCom_Poll();

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

    /* Other application work, including CDC0, can run here. */
}
```

This is an **example loop**, not a second loop to add alongside the one already in `main.c`. Integrate the echo block into your existing main loop.

### Example: send an application message on CDC1

```c
static const uint8_t message[] = "Hello from CDC1\r\n";

if (USB_Port1Ready())
{
    int accepted = USB_Port1Write(message, sizeof(message) - 1U);
    /* If accepted is less than sizeof(message) - 1U,
       retry the remaining bytes in a later main-loop iteration. */
    (void)accepted;
}
```

This example demonstrates the API but does not implement a full retry mechanism. For a message that must be delivered completely, maintain a pending buffer and offset across loop iterations; never resend already-accepted bytes.

### Example: process commands instead of echoing

```c
int ch = USB_Port1GetChar();
if (ch >= 0)
{
    /* Process the byte as part of your own command or binary protocol. */
    process_port1_byte((uint8_t)ch);
}
```

`process_port1_byte()` is a placeholder for your application's function. Removing the echo logic from `main.c` does not affect CDC0.

## CDC0: console and printf

The first port has its own API:

```c
int USB_ConsoleReady(void);
int USB_ConsoleGetChar(void);
int USB_ConsoleWrite(const uint8_t *data, unsigned int length);
```

`USB_ConsoleGetChar()` returns a byte or `-1` if none is available. `USB_ConsoleWrite()` is nonblocking and returns the number of bytes queued. `USB_ConsoleReady()` indicates whether CDC0 is ready.

`printf()` and `fprintf(stderr, ...)` are routed to CDC0 by `_write()` in `dual_usb_com/retarget_stdio.c`. In the working CMake layout, **compile `retarget_stdio.c` directly into the executable**, not into the `dual_usb_com` static library:

```cmake
add_subdirectory(dual_usb_com)

add_executable(${PROJECT_NAME}
    main.c
    syscalls.c
    dual_usb_com/retarget_stdio.c
)

target_link_libraries(${PROJECT_NAME} PRIVATE dual_usb_com)
```

Keep `retarget_stdio.c` **out of** the `add_library(dual_usb_com ...)` source list. This avoids the static-library syscall selection issue that previously caused `printf()` to fail. `syscalls.c` supplies the remaining bare-metal Newlib syscall stubs and is also compiled directly into the executable. There is no need for a custom `_write_r()` wrapper.

The current `_write()` is nonblocking. A full USB transmit FIFO can cause short writes or a Newlib output error. For production logging, consider a dedicated log queue or a retry policy appropriate for the application. When reconnecting CDC0, the example clears any prior `stdout` error state with `clearerr(stdout)` before printing its welcome message.

## Reusing `dual_usb_com` in another application

Copy the `dual_usb_com/` directory and link its CMake target using `add_subdirectory(dual_usb_com)` and `target_link_libraries(your_app PRIVATE dual_usb_com)`. Call `BOARD_InitHardware()`, `APPInit()`, and `DualUsbCom_Poll()` as above. Use the `USB_Console*` and `USB_Port1*` APIs as needed.

For `printf()` redirection, compile `dual_usb_com/retarget_stdio.c` into **your application executable**. Also provide the bare-metal syscalls required by your C library (for example, `syscalls.c`). If your application has its own `_write()` implementation, do not link a second one.

**Portability scope:** The bundled implementation is currently tailored to MCXN236 / `ez_mcxn`, with NXP USB stack sources, board/clock configuration, generated headers, SDK paths and an MCXN236 linker script. Reusing it on a different MCU or board requires adapting those dependencies.

## Troubleshooting

- **Both ports appear, but CDC0 is silent:** Confirm the terminal asserts DTR, then verify `_write()` is linked from your application object, not inadvertently selected from `libnosys`.
- **CDC1 does not echo:** Check that `main.c` still executes the CDC1 echo block and calls `DualUsbCom_Poll()` continuously.
- **Only part of a CDC1 message is sent:** Handle the return value from `USB_Port1Write()` and retry remaining bytes later.
- **Data disappears under sustained traffic:** Both FIFOs have finite capacity. Service the loop frequently and consider flow control or application-level buffering.
