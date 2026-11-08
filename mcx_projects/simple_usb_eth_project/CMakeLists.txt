# Standalone CMake / Ninja / GCC project; no West or Kconfig.
cmake_minimum_required(VERSION 3.22)
project(simple_usb_project LANGUAGES C ASM)

# set(MCUX_SDK_ROOT "C:/DEV/projects/mcuxpresso-sdk/mcuxsdk" CACHE PATH "MCUXpresso SDK root")
# set(BOARD_ROOT "C:/DEV/projects/mcx_projects/_boards/ez_mcxn" CACHE PATH "Custom board root")
set(MCUX_SDK_ROOT $ENV{MCUXPRESSO_SDK_ROOT})   
set(BOARD_ROOT "$ENV{MCXN_CUSTOM_BOARD_ROOT}/ez_mcxn")   
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LWIP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lwip")
include("${LWIP_DIR}/src/Filelists.cmake")
add_subdirectory(usb_com_eth)
target_sources(usb_com_eth PRIVATE ${lwipcore_SRCS} ${lwipcore4_SRCS} ${LWIP_DIR}/src/netif/ethernet.c)
target_include_directories(usb_com_eth PUBLIC "${LWIP_DIR}/src/include")
add_executable(${PROJECT_NAME} 
    main.c
    syscalls.c
    usb_com_eth/retarget_stdio.c
)
target_link_libraries(${PROJECT_NAME} PRIVATE usb_com_eth)

set(LINKER_SCRIPT "${MCUX_SDK_ROOT}/devices/MCX/MCXN/MCXN236/gcc/MCXN236_flash.ld")
target_link_options(${PROJECT_NAME} PRIVATE
    -mthumb -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16
    --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections -Wl,-static -Wl,--print-memory-usage
    -Wl,--no-warn-rwx-segments
    -Wl,--defsym=__stack_size__=0x2000
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map"
    "-T${LINKER_SCRIPT}"
)
target_link_libraries(${PROJECT_NAME} PRIVATE -Wl,--start-group m c gcc nosys -Wl,--end-group)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin"
    COMMENT "Generating binary image"
)
