cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED PICO_SDK_PATH)
    if (DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
    else()
        message(FATAL_ERROR "PICO_SDK_PATH is required (install Raspberry Pi Pico extension)")
    endif()
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(speaking_clock_c C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# First, deliberately BT-free, hardware bring-up. No RTC writes or flash settings.
add_executable(speaking_clock_c
    main.c
    drivers/ds1302.c
    drivers/lcd1602_i2c.c
    app/countdown.c
    app/alarm_engine.c
    drivers/controls.c
    drivers/dfplayer.c
    audio/queue.c
    audio/speech.c
    services/settings_store.c
)
target_include_directories(speaking_clock_c PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/drivers
    ${CMAKE_CURRENT_LIST_DIR}/app
    ${CMAKE_CURRENT_LIST_DIR}/audio
    ${CMAKE_CURRENT_LIST_DIR}/services
)
target_link_libraries(speaking_clock_c PRIVATE
    pico_stdlib
    hardware_i2c
    hardware_uart
    hardware_flash
    pico_flash
    pico_bootrom
)
pico_enable_stdio_usb(speaking_clock_c 1)
pico_enable_stdio_uart(speaking_clock_c 0)
pico_add_extra_outputs(speaking_clock_c)
