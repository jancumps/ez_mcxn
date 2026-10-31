cmake_minimum_required(VERSION 3.22.0)

include("${SdkRootDirPath}/cmake/extension/mcux.cmake")

project(
  dual_usb_com_ports LANGUAGES ASM C CXX)

include("${SdkRootDirPath}/CMakeLists.txt")

mcux_add_macro(
  CC "-DUSB_STACK_BM"
)

mcux_add_source(
  SOURCES
    composite.c
    composite.h
    usb_device_cdc_acm.c
    usb_device_cdc_acm.h
    usb_device_ch9.c
    usb_device_ch9.h
    usb_device_descriptor.c
    usb_device_descriptor.h
    virtual_com.c
    virtual_com.h
    hardware_init.c
    retarget_stdio.c
)

mcux_convert_binary(
  BINARY "${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin"
)
