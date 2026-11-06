# EZ-MCXN

This repository contains NXP MCXN related content.

# MCXN236 Test Board

This test board is intended for trying out the MCXN236VKLT microcontroller (100-pin HLQFP 0.5 mm package). The 235 variant is also possible, and there's a good chance the MCXN547VKLT microcontroller will work too.

![0.75](readme.assets/kicad_render.png)

# MCX Projects

This folder contains a board config and example blinky application source code. The folders may be hard-coded in places; the mcx\_projects folder can be placed at `c:\dev\projects` to keep things seamless, otherwise you may need to edit files.

# Environment Variables

The following user vars (environment variables) will need to be created:

`ARMGCC_DIR = C:\DEV\arm_gnu_toolchains\15.2.rel1 `(this folder should contain bin, include, lib folders and so on)

`MCUXPRESSO_SDK_ROOT = C:\dev\projects\mcuxpresso-sdk\mcuxsdk`

`MCXN_CUSTOM_BOARD_ROOT = C:\dev\projects\mcx_projects\_boards`


# Double Blinky

## Overview

Double Blinky is a simple MCUXpresso SDK project that blinks two LEDs.

The example uses:

* LED1: P3_6
* LED2: P3_1

Both LEDs are active high.

The project also provides a simple starting point for creating new projects using the EZ-MCXN board configuration.

## Prerequisites

The following are required:

* MCUXpresso SDK installed under `C:\dev\projects\mcuxpresso-sdk`
* Arm GNU Toolchain
* CMake and Ninja
* Python
* MCUXpresso Config Tools
* NXP Secure Provisioning SDK (SPSDK), installed by typing: `py -m pip install spsdk`
* `west`

The project workspace is expected to be:

```text
C:\dev\projects\mcx_projects
```

For example:

```text
C:\dev\projects\mcx_projects\
├── _boards\
│   └── ez_mcxn\
└── double_blinky\
```

The `MCUXPRESSO_SDK_ROOT` environment variable must point to the MCUXpresso SDK:

```text
C:\dev\projects\mcuxpresso-sdk\mcuxsdk
```

## Building the Project

Open PowerShell in the project directory:

```powershell
cd C:\dev\projects\mcx_projects\double_blinky
```

One-time, you may need to enable permissions by typing:
```
Unblock-File .\build.ps1
```

For a clean build:

```powershell
.\build.ps1 clean
.\build.ps1 build debug
```

The build output is placed in:

```text
double_blinky\output
```

The main generated files are:

```text
output\double_blinky.elf
output\double_blinky.bin
```

Useful build commands are:

```powershell
.\build.ps1
.\build.ps1 clean
.\build.ps1 debug
.\build.ps1 release
.\build.ps1 menuconfig
.\build.ps1 list
.\build.ps1 flash
```

The commands have the following purposes:

* `.\build.ps1` - Builds the project using the debug build configuration.
* `.\build.ps1 clean` - Cleans compiled files from the existing build. Use this after changing board files, Kconfig settings or CMake files, or if a normal build behaves unexpectedly. Also, do this whenever changing between debug and release builds.
* `.\build.ps1 release` - Builds the project using the debug build configuration.
* `.\build.ps1 menuconfig` - Opens the interactive Kconfig configuration interface, allowing SDK and project configuration options to be viewed or changed.
* `.\build.ps1 list` - Displays the project/build configuration being used by the script, such as the board, SDK, toolchain and build directory.
* * `.\build.ps1 flash` - Uses SPSDK to flash the microcontroller. Before executing this command, hold down the BOOT button and reset the microcontroller, then release the BOOT button.

For normal source-code changes, for debug builds, `.\build.ps1` is usually sufficient. Use `clean` after changing the board configuration, Kconfig files or CMake files.

## Creating a New Project

`double_blinky` can be used as a template for another application.

Copy the project folder and give it a new name. For example:

```text
C:\dev\projects\mcx_projects\testproj
```

Then:

1. In `CMakeLists.txt`, change the project name from `double_blinky` to `testproj`.
2. In `Kconfig`, change the displayed project name.
3. Modify `main.c` for the new application.
4. For VS Code IntelliSense Syntax Highlighting to work well, edit the file `mcx_projects\.vscode\c_cpp_properties.json` to add your project folder in the `compileCommands` section. Note: For IntelliSense to fully work, open the top folder `mcx_projects`, not the individual project folder. You may need to restart VS Code after performing a first build for each project, for IntelliSense to fully work, because it relies on a file that gets generated and placed in the project folder output subdirectory.
5. Build with:

```powershell
.\build.ps1 debug
```

The existing `ez_mcxn` board configuration can be shared by multiple projects.

## Board Configuration

Board configurations are stored in:

```text
C:\dev\projects\mcx_projects\_boards
```

The board used by this project is:

```text
C:\dev\projects\mcx_projects\_boards\ez_mcxn
```

The important files include:

```text
ez_mcxn\
├── board\
├── board.c
├── board.h
├── CMakeLists.txt
├── EZ_MXCN.mex
├── Kconfig
├── Kconfig.defconfig
├── prj.conf
└── variable.cmake
```

The `board` subfolder contains files generated by MCUXpresso Config Tools, such as:

```text
clock_config.c
clock_config.h
peripherals.c
peripherals.h
pin_mux.c
pin_mux.h
```

Do not normally edit these generated files manually.

## Adding or Modifying Peripherals

Launch MCUXpresso Config Tools and open:

```text
C:\dev\projects\mcx_projects\_boards\ez_mcxn\EZ_MXCN.mex
```

Use Config Tools to modify pins, clocks and peripherals.

When finished, use **Update Code** to regenerate the files in the `board` subfolder.

Then rebuild the application:

```powershell
.\build.ps1 clean
.\build.ps1 debug
```

## Creating a New Board Configuration

If a project needs different hardware without changing the shared `ez_mcxn` configuration, make a copy of the board folder.

For example:

```text
C:\dev\projects\mcx_projects\_boards\myboard
```

Update the board name where required, particularly in:

```text
Kconfig
variable.cmake
board.h
```

Rename the `.mex` file if desired, open it with MCUXpresso Config Tools, make the required pin, clock and peripheral changes, and run **Update Code**.

Finally, change the `$Board` setting in the project's `build.ps1`:

```powershell
$Board = "myboard"
```

Then perform a clean build.

## EZ-MCXN Hardware

The current EZ-MCXN configuration uses:

* MCU: MCXN236
* Main crystal: 24 MHz
* RTC crystal: 32.768 kHz
* System clock: 150 MHz
* LED1: P3_6, active high
* LED2: P3_1, active high
* UART RX: P1_8
* UART TX: P1_9
* UART: LP_FLEXCOMM4 / LPUART
* UART baud rate: 115200

## Debugging and Programming

### Using a Debug Probe

A compatible SWD debug probe, for example MCU-Link, can be used for programming and debugging.

Build the project first using:

```powershell
.\build.ps1 debug
```

The ELF file for debugging/programming is then available at:

```text
output\double_blinky.elf
```

### Programming Without a Debug Probe

The MCXN236 contains a ROM-based ISP (In-System Programming) bootloader. This makes it possible to program the internal flash without an SWD debug probe.

The ROM bootloader supports several communication interfaces, including USB HID.

The EZ-MCXN board has a USB connector connected to the MCU USB signals and push-buttons labelled `BOOT LOAD` and `RESET`.

To enter ROM ISP mode:

1. Connect the EZ-MCXN USB connector to the PC.
2. Press and hold `BOOT LOAD`.
3. While continuing to hold `BOOT LOAD`, press and release `RESET`.
4. Release `BOOT LOAD`.

The MCXN236 should now be running its built-in ROM ISP bootloader rather than the application in flash.

If you installed NXP SPSDK (mentioned earlier) then afer you've pressed the buttons to get into the bootloader mode, type:
```
.\build.ps1 flash
```

If all goes well, the new firmware will start running!

The PC can communicate with the ROM bootloader using NXP's `blhost` command-line utility or MCUXpresso Secure Provisioning Tool (the above command uses the Python SPSDK install, view the contents of the `build.ps1` file to see the commands that were used).

A useful first test with `blhost` is to ask the bootloader for its version/property information. For USB HID, `blhost` uses the USB VID/PID of the ROM bootloader.

For example, the MCXN236 ROM ISP used on NXP's FRDM-MCXN236 is accessible with:

```powershell
blhost -u 0x1FC9,0x0158 -- get-property 1
```

If this succeeds, the PC is communicating with the MCXN236 ROM bootloader.

The application binary produced by this project is:

```text
output\double_blinky.bin
```

For an unsecured development device using the normal internal-flash boot configuration, firmware can be written to the beginning of internal flash using the ROM ISP tools.

NXP's MCUXpresso Secure Provisioning Tool is recommended when initially setting up or verifying this process because it provides a GUI for selecting the MCXN236, establishing the ISP connection, preparing the image and programming the device.

After programming is complete, press `RESET` without holding `BOOT LOAD`. The MCU should then boot the application normally from internal flash.

> **Note:** ROM ISP is different from installing a custom USB DFU bootloader. The MCXN236 ROM bootloader is already built into the chip and cannot be accidentally erased with the application. Secure-boot, lifecycle and CMPA configuration can change what the ROM permits, so the simple procedure above assumes an unsecured development device with the normal internal-flash boot configuration.

# EZ_MCXN Pin Allocations
<!-- info: table: widths="6% 16% 6% 18% 6% 21% 6% 21%" -->

| Pin | Use           | Pin | Use               | Pin | Use       | Pin | Use                         |
| :-- | :------------ | :-- | :---------------- | :-- | :-------- | :-- | :-------------------------- |
| 1   | UART\_RXD     | 26  | HDR\_PORT4        | 51  | SPARE(TP) | 76  | SWDIO                       |
| 2   | UART\_TXD     | 27  | HDR\_PORT4        | 52  | 0V        | 77  | SWCLK                       |
| 3   | CANTX         | 28  | HDR\_PORT4        | 53  | N/C       | 78  | SWO                         |
| 4   | CANRX         | 29  | HDR\_PORT4        | 54  | 3.3V      | 79  | SPARE(TP)                   |
| 5   | HDR\_PORT1    | 30  | 3.3V via FBEAD    | 55  | Cap to 0V | 80  | PDM\_CLM                    |
| 6   | HDR\_PORT1    | 31  | 3.3V via FBEAD    | 56  | TFT\_DC   | 81  | PDM\_DAT                    |
| 7   | HDR\_PORT1    | 32  | 0V                | 57  | TFT\_RST  | 82  | BOOT\_BTN\_CAP\_AND\_PULLUP |
| 8   | HDR\_PORT1    | 33  | 0V                | 58  | Cap to 0V | 83  | 3.3V                        |
| 9   | RST (R/C/BTN) | 34  | 3.3V              | 59  | 3.3V      | 84  | I2C\_SDA                    |
| 10  | XTAL          | 35  | HDR\_PORT4GENERAL | 60  | TFT\_CS   | 85  | I2C\_SCL                    |
| 11  | EXTAL         | 36  | HDR\_PORT4GENERAL | 61  | TFT\_BL   | 86  | PDM\_DAT1                   |
| 12  | Short to 58   | 37  | HDR\_PORT4GENERAL | 62  | KEY\_ROW1 | 87  | SPARE(TP)                   |
| 13  | 3.3V          | 38  | HDR\_PORT4GENERAL | 63  | KEY\_ROW2 | 88  | microSD MOSI                |
| 14  | I2S\_A\_BCLK  | 39  | HDR\_PORT4GENERAL | 64  | KEY\_ROW3 | 89  | microSD SCK                 |
| 15  | I2S\_A\_LRCLK | 40  | USBDP             | 65  | KEY\_ROW4 | 90  | microSD MISO                |
| 16  | I2S\_B\_DAT   | 41  | USBDM             | 66  | 3.3V      | 91  | microSD CS                  |
| 17  | I2S\_A\_DAT   | 42  | USB\_VBUS         | 67  | KEY\_COL1 | 92  | TFT\_MOSI                   |
| 18  | I2S\_A\_DAT1  | 43  | 0V                | 68  | KEY\_COL2 | 93  | TFT\_SCK                    |
| 19  | I2S\_B\_DAT1  | 44  | 3.3V              | 69  | KEY\_COL3 | 94  | TFT\_MISO                   |
| 20  | I2S\_B\_BCLK  | 45  | NC/DP             | 70  | KEY\_COL4 | 95  | HDR\_PORT1                  |
| 21  | I2S\_B\_LRCLK | 46  | NC/DM             | 71  | I2S\_MCLK | 96  | 3.3V                        |
| 22  | HDR\_PORT4    | 47  | 3.3V              | 72  | LED1      | 97  | HDR\_PORT1                  |
| 23  | HDR\_PORT4    | 48  | EXTAL32K          | 73  | 3.3V      | 98  | HDR\_PORT1                  |
| 24  | HDR\_PORT4    | 49  | XTAL32K           | 74  | LED2      | 99  | HDR\_PORT1                  |
| 25  | HDR\_PORT4    | 50  | N/C               | 75  | TFT\_CS2  | 100 | HDR\_PORT1                  |

# EZ-MCXN Parts List

Here is the parts list for the EZ-MCXN board:

| Reference          | Qty | Value          | Description                                                          |
| ------------------ | --- | -------------- | -------------------------------------------------------------------- |
| R10, R11           | 2   | 5.1k           | 0603 Resistor                                                        |
| R12, R13, R16, R17 | 4   | 10k            | 0603 Resistor                                                        |
| R14, R15           | 2   | 1k             | 0603 Resistor                                                        |
| R20                | 1   | 0R             | 0402 zero-ohm Resistor                                               |
| C1-C12, C16        | 13  | 100n           | 0402 Ceramic Capacitor X7R                                           |
| C13-C15, C24, C25  | 5   | 1u             | 0603 Ceramic Capacitor X7R                                           |
| C17                | 1   | 4.7u           | 0805 Ceramic Capacitor X7R                                           |
| C18                | 1   | 2.2u           | 0805 Ceramic Capacitor X7R                                           |
| C19                | 1   | 10u            | 0805 Ceramic Capacitor X5R 10V                                       |
| C21, C22           | 2   | 10u            | Case B Tantalum Capacitor 10V e.g. TPSB106K016R0800                  |
| C23                | 1   | 1n             | 0603 Ceramic Capacitor                                               |
| C26, C27           | 2   | 4.7p           | 0603 Ceramic Capacitor                                               |
| C29                | 1   | 1u             | 0805 Ceramic Capacitor                                               |
| D1, D2             | 2   | LED            | 0805 LED (0603 fits too) D1=RED, D2=BLUE                             |
| D3, D4             | 2   | MBR0540        | SOD-123 Schottky Diode                                               |
| FB1, FB2           | 2   | BLM21AG121SH1D | SMD Ferrite Bead 0805 120 ohm                                        |
| J1                 | 1   | USB4085        | USB Type C Receptacle Through-Hole                                   |
| J2-J11             | 1   | Header         | Dual Row 2.54mm Pin Header (total 2 x 42 way, cut to required sizes) |
| J12                | 1   | MicroSD_socket | Same Sky MSD-4-A and possibly Molex 5027740891                       |
| J13                | 1   | Keypad         | 4x4 or 4x3 keypad and 7- or 8-way header pin or socket if required   |
| J14                | 1   | Terminal Block | 2-way Screw Terminal Block 3.5 or 3.81mm pitch                       |
| SW1-SW3            | 3   | SKQGAFE010     | Tact button SMD 5x5mm body 3.7x6.2mm pitch                           |
| SW4, SW5           | 2   | TS-1101        | Tact button SMD 6x3.6mm                                              |
| U1                 | 1   | MCXN236VKLT    | NXP  MCXN236/235/547VKLT HLQFP100                                    |
| U2                 | 1   | MIC5209-3.3YS  | 3.3V LDO SOT-223                                                     |
| U3                 | 1   | SRV05-4        | ESD Diode Array                                                      |
| Y1                 | 1   | 830108212309   | Wurth 24MHz Crystal SMD 2016 4-pin 2.0x1.6mm                         |
| Y2                 | 1   | 830009678      | Wurth 32.768kHz Crystal SMD 2-pin 3.2x1.5mm                          |

# MINI-MCXN Board
The MINI-MCXN is a very cut-down board, with just 8 GPIO exposed via a flat-flex connector, and pads for attaching wires to PDM mics.

![0.75](readme.assets/mini_kicad_render.png)

## MINI-MCXN Parts List

| Reference | Qty | Value             | Description                                           |
| --------- | --- | ----------------- | ----------------------------------------------------- |
| C1-C12    | 12  | 100n              | 0402 Capacitor                                        |
| C13-C15   | 3   | 1u                | 0603 Capacitor                                        |
| C17       | 1   | 4.7u              | 0805 Capacitor                                        |
| C18       | 1   | 2.2u              | 0805 Capacitor                                        |
| C19       | 1   | 10u               | 0805 Capacitor                                        |
| C21, C22  | 2   | 10u               | Case B Tantalum Capacitor 10V e.g. TPSB106K016R0800   |
| C23       | 1   | 1n                | 0603 Capacitor                                        |
| C26, C27  | 2   | 4.7p              | 0402 Capacitor                                        |
| C29       | 1   | 1u                | 0805 Capacitor                                        |
| D1        | 1   | LED               | 0603 LED RED                                          |
| FB1, FB2  | 2   | BLM21AG121SH1D    | SMD Ferrite Bead 0805 120 ohm                         |
| J1        | 1   | USB4085           | USB Type C Receptacle Through-Hole                    |
| J2        | 1   | JST SH 4-way Conn | Connector JST SH SM04B-SRSS-TB 1mm pitch Horiz        |
| J5        | 1   | FFC_0.5MM 16-way  | FFC Connector Hirose FH12-16S-0.5SH 0.5mm pitch Horiz |
| R10, R11  | 2   | 5.1k              | 0402 Resistor                                         |
| R12, R13  | 2   | 10k               | 0402 Resistor                                         |
| R14       | 1   | 1k                | 0402 Resistor                                         |
| SW1-SW3   | 3   | SKQGAFE010        | Tact button SMD 5x5mm body 3.7x6.2mm pitch            |
| SW4, SW5  | 2   | Tact 4x3mm        | Tact button 4x3mm body 2-pin                          |
| U1        | 1   | MCXN236VKLT       | NXP MCXN236/235/547VKLT HLQFP100                      |
| U2        | 1   | MIC5209-3.3YS     | 3.3V LDO SOT-223                                      |
| U3        | 1   | SRV05-4           | ESD Diode Array                                       |
| Y1        | 1   | 830108212309      | Wurth 24MHz Crystal SMD 2016 4-pin 2.0x1.6mm          |



