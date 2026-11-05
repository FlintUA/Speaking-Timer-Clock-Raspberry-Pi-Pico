cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED PICO_SDK_PATH)
    if (DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    else()
        message(FATAL_ERROR "PICO_SDK_PATH is not set")
    endif()
endif()

if (NOT DEFINED PICO_EXAMPLES_PATH)
    message(FATAL_ERROR "PICO_EXAMPLES_PATH is not set")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(nest_a2dp_autotest C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(nest_a2dp_autotest
    main.c
    ${PICO_SDK_PATH}/lib/btstack/example/a2dp_source_demo.c
    ${PICO_SDK_PATH}/lib/btstack/src/btstack_audio_generator.c
)

target_link_libraries(nest_a2dp_autotest PRIVATE
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_cyw43
    pico_btstack_ble
    pico_btstack_classic
    pico_btstack_sbc_encoder
)

target_include_directories(nest_a2dp_autotest PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${PICO_EXAMPLES_PATH}/bluetooth/config
    ${PICO_SDK_PATH}/lib/btstack/3rd-party/hxcmod-player
    ${PICO_SDK_PATH}/lib/btstack/3rd-party/hxcmod-player/mods
)

# Deliberately do NOT define HAVE_BTSTACK_STDIN or ENABLE_MODPLAYER.
# Upstream behavior then becomes:
# HCI ready -> automatic speaker scan -> automatic A2DP stream -> sine source.

pico_enable_stdio_usb(nest_a2dp_autotest 1)
pico_enable_stdio_uart(nest_a2dp_autotest 0)

pico_add_extra_outputs(nest_a2dp_autotest)


add_executable(usb_stdio_test
    usb_stdio_test.c
)

target_link_libraries(usb_stdio_test PRIVATE
    pico_stdlib
)

pico_enable_stdio_usb(usb_stdio_test 1)
pico_enable_stdio_uart(usb_stdio_test 0)

pico_add_extra_outputs(usb_stdio_test)
