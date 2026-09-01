# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Optional log categories. Both are off by default so a normal build stays quiet.
option(ENABLE_USB_LOGGING "Log USB device events and HID report traffic" OFF)
option(ENABLE_HEARTBEAT_LOGS "Log a periodic Core 0 heartbeat" OFF)

# Baud rate of the log UART. Lower it for a receiver that cannot keep up, such
# as a bit-banged software UART on an AVR.
set(UART_BAUD_RATE 115200 CACHE STRING "Baud rate of the log UART")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(xbox_pico C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable.  
add_executable(xbox_pico
    main.c
    usb_descriptors.c
    bridge_mailbox.c
    ble_hid_bridge.c
    ble_bridge_policy.c
    hid_report_parser.c
    pairing_store.c
    bt_example_common.c
    )  

# Apply strict warnings to project-owned translation units. Pico SDK, BTstack,
# and TinyUSB are compiled into the same final target, so target-wide warning
# flags would misleadingly report warnings in pinned third-party sources.
set_source_files_properties(
    main.c
    usb_descriptors.c
    bridge_mailbox.c
    ble_hid_bridge.c
    ble_bridge_policy.c
    hid_report_parser.c
    pairing_store.c
    bt_example_common.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror;-fstack-usage"
    )
target_link_libraries(xbox_pico
    pico_stdlib
    pico_multicore
    pico_flash # flash_safe_execute_core_init(), used in main()
    hardware_sync
    pico_unique_id
    tinyusb_device
    tinyusb_board
    pico_btstack_ble
    pico_btstack_cyw43
    pico_btstack_flash_bank
    pico_cyw43_arch_threadsafe_background
    )   
target_include_directories(xbox_pico PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    )
target_compile_definitions(xbox_pico PRIVATE
    CYW43_LWIP=0
    PICO_CORE1_STACK_SIZE=0
    PICO_DEFAULT_UART_BAUD_RATE=${UART_BAUD_RATE}
    )    
    
if(ENABLE_USB_LOGGING)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_USB_LOGGING=1)
endif()    
    
if(ENABLE_HEARTBEAT_LOGS)
    target_compile_definitions(
        ${PROJECT_NAME}
        PRIVATE ENABLE_HEARTBEAT_LOGS=1
    )
endif()    
    
pico_btstack_make_gatt_header(xbox_pico PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge.gatt
    )

# disable usb output, enable uart output
pico_enable_stdio_usb(xbox_pico 0)
pico_enable_stdio_uart(xbox_pico 1)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(xbox_pico)

function(add_pairing_clear_target target_name clear_mask)
    add_executable(${target_name}
        maintenance_clear.c
        pairing_store.c
        bt_example_common.c
        )
    set_source_files_properties(
        maintenance_clear.c
        pairing_store.c
        bt_example_common.c
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror"
        )
    target_compile_definitions(${target_name} PRIVATE
        CLEAR_ROLE_MASK=${clear_mask}
        CYW43_LWIP=0
        PICO_DEFAULT_UART_BAUD_RATE=${UART_BAUD_RATE}
        )
    target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target_name}
        pico_stdlib
        pico_btstack_ble
        pico_btstack_cyw43
        pico_btstack_flash_bank
        pico_cyw43_arch_threadsafe_background
        )
    pico_enable_stdio_usb(${target_name} 0)
    pico_enable_stdio_uart(${target_name} 1)
    pico_add_extra_outputs(${target_name})
endfunction()

add_pairing_clear_target(xbox_pico_clear_keyboard 1)
add_pairing_clear_target(xbox_pico_clear_mouse 2)
add_pairing_clear_target(xbox_pico_clear_all 3)
