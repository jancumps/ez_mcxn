add_library(dual_usb_com STATIC
    "${MCUX_SDK_ROOT}/devices/MCX/MCXN/MCXN236/system_MCXN236.c"
    "${MCUX_SDK_ROOT}/devices/MCX/MCXN/MCXN236/gcc/startup_MCXN236.S"
    "${MCUX_SDK_ROOT}/devices/MCX/MCXN/MCXN236/drivers/fsl_clock.c"
    "${MCUX_SDK_ROOT}/devices/MCX/MCXN/MCXN236/drivers/fsl_reset.c"
    "${BOARD_ROOT}/board.c"
    "${BOARD_ROOT}/board/clock_config.c"
    "${BOARD_ROOT}/board/pin_mux.c"
    "${BOARD_ROOT}/board/peripherals.c"
    "${MCUX_SDK_ROOT}/drivers/common/fsl_common.c"
    "${MCUX_SDK_ROOT}/drivers/common/fsl_common_arm.c"
    "${MCUX_SDK_ROOT}/drivers/gpio/fsl_gpio.c"
    "${MCUX_SDK_ROOT}/drivers/lpflexcomm/fsl_lpflexcomm.c"
    "${MCUX_SDK_ROOT}/drivers/mcx_spc/fsl_spc.c"
    "${MCUX_SDK_ROOT}/drivers/lpflexcomm/lpuart/fsl_lpuart.c"
    "${MCUX_SDK_ROOT}/components/debug_console/fsl_debug_console.c"
    "${MCUX_SDK_ROOT}/components/lists/fsl_component_generic_list.c"
    "${MCUX_SDK_ROOT}/components/misc_utilities/fsl_syscall_stub.c"
    "${MCUX_SDK_ROOT}/components/misc_utilities/fsl_sbrk.c"
    "${MCUX_SDK_ROOT}/components/misc_utilities/fsl_memcpy.S"
    "${MCUX_SDK_ROOT}/components/misc_utilities/fsl_memset.S"
    "${MCUX_SDK_ROOT}/components/osa/fsl_os_abstraction_bm.c"
    "${MCUX_SDK_ROOT}/components/serial_manager/fsl_component_serial_manager.c"
    "${MCUX_SDK_ROOT}/components/serial_manager/fsl_component_serial_port_uart.c"
    "${MCUX_SDK_ROOT}/components/str/fsl_str.c"
    "${MCUX_SDK_ROOT}/components/uart/fsl_adapter_lpuart.c"
    "${MCUX_SDK_ROOT}/middleware/usb/device/usb_device_ehci.c"
    "${MCUX_SDK_ROOT}/middleware/usb/device/usb_device_dci.c"
    "${MCUX_SDK_ROOT}/middleware/usb/phy/usb_phy.c"
    composite.c
    virtual_com.c
    usb_device_cdc_acm.c
    usb_device_ch9.c
    usb_device_descriptor.c
    hardware_init.c
)

target_include_directories(dual_usb_com PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/config"
    "${MCUX_SDK_ROOT}/devices/MCX/MCXN/MCXN236"
    "${MCUX_SDK_ROOT}/devices/MCX/MCXN/periph1"
    "${MCUX_SDK_ROOT}/devices/MCX/MCXN/MCXN236/drivers"
    "${MCUX_SDK_ROOT}/arch/arm/CMSIS/Core/Include"
    "${MCUX_SDK_ROOT}/arch/arm/CMSIS/Core/Include/m-profile"
    "${BOARD_ROOT}"
    "${BOARD_ROOT}/board"
    "${MCUX_SDK_ROOT}/drivers/common"
    "${MCUX_SDK_ROOT}/drivers/gpio"
    "${MCUX_SDK_ROOT}/drivers/lpflexcomm"
    "${MCUX_SDK_ROOT}/drivers/mcx_spc"
    "${MCUX_SDK_ROOT}/drivers/port"
    "${MCUX_SDK_ROOT}/drivers/lpflexcomm/lpuart"
    "${MCUX_SDK_ROOT}/components/debug_console"
    "${MCUX_SDK_ROOT}/components/lists"
    "${MCUX_SDK_ROOT}/components/osa"
    "${MCUX_SDK_ROOT}/components/serial_manager"
    "${MCUX_SDK_ROOT}/components/str"
    "${MCUX_SDK_ROOT}/components/uart"
    "${MCUX_SDK_ROOT}/middleware/usb/include"
    "${MCUX_SDK_ROOT}/middleware/usb/device"
    "${MCUX_SDK_ROOT}/middleware/usb/phy"
)

# These definitions are taken from the known-good compile_commands.json.
target_compile_definitions(dual_usb_com PUBLIC
    CPU_MCXN236VNL MCXN236_SERIES OSA_USED USE_RTOS=0
    SERIAL_PORT_TYPE_UART=1 USB_STACK_BM
)

# Saved generated Kconfig output; no Kconfig processing is invoked.
# Version header is intentionally not forced in; add it only if SDK code requires it.
target_compile_options(dual_usb_com PUBLIC
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_LIST_DIR}/config/mcux_config.h>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_LIST_DIR}/config/mcuxsdk_version.h>

    -mthumb
    -mcpu=cortex-m33
    -mfloat-abi=hard
    -mfpu=fpv5-sp-d16

    -g
    -O0
    -ffunction-sections
    -fdata-sections
    -fno-common

    $<$<COMPILE_LANGUAGE:C>:-std=gnu99>
    $<$<COMPILE_LANGUAGE:C>:--specs=nano.specs>
)

