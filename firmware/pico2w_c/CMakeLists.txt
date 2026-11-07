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
    drivers/sd_spi.c
    drivers/sd_fat32.c
    audio/queue.c
    audio/router.c
    audio/speech.c
    audio/mp3_probe.c
    audio/pcm_ring.c
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
    hardware_spi
    hardware_uart
    hardware_flash
    pico_flash
    pico_bootrom
)
pico_enable_stdio_usb(speaking_clock_c 1)
pico_enable_stdio_uart(speaking_clock_c 0)
pico_add_extra_outputs(speaking_clock_c)

# The known-good production build keeps this OFF.
option(CLOCK_ENABLE_A2DP "Experimental BTstack A2DP on Pico 2 W" OFF)
if(CLOCK_ENABLE_A2DP)
    if(NOT PICO_BOARD STREQUAL "pico2_w")
        message(FATAL_ERROR "A2DP requires PICO_BOARD=pico2_w")
    endif()
    if(NOT DEFINED PICO_EXAMPLES_PATH)
        get_filename_component(_pico_home "${PICO_SDK_PATH}/../.." ABSOLUTE)
        set(PICO_EXAMPLES_PATH "${_pico_home}/pico-examples")
    endif()
    if(NOT EXISTS "${PICO_EXAMPLES_PATH}/bluetooth/config/btstack_config_common.h")
        message(FATAL_ERROR "Set PICO_EXAMPLES_PATH to your pico-examples checkout")
    endif()
    set(_a2dp_test "${CMAKE_CURRENT_LIST_DIR}/../../experiments/bluetooth_a2dp_nest/nest_autotest")
    set(_a2dp_orig "${PICO_SDK_PATH}/lib/btstack/example/a2dp_source_demo.c")
    set(_a2dp_generated "${CMAKE_CURRENT_BINARY_DIR}/clock_a2dp_source_generated.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_a2dp_orig}" "${_a2dp_test}/patch_a2dp_source.cmake")
    set(CLOCK_A2DP_TARGET_ADDR "" CACHE STRING "Known A2DP target (experimental; local build only)")
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DUPSTREAM_SOURCE=${_a2dp_orig}"
        "-DPATCHED_SOURCE=${_a2dp_generated}"
        "-DAUTO_TARGET_ADDR=${CLOCK_A2DP_TARGET_ADDR}"
        -P "${_a2dp_test}/patch_a2dp_source.cmake"
        RESULT_VARIABLE _patch_result)
    if(NOT _patch_result EQUAL 0)
        message(FATAL_ERROR "Could not prepare A2DP source from installed SDK")
    endif()
    target_sources(speaking_clock_c PRIVATE
        "${_a2dp_generated}"
        "${PICO_SDK_PATH}/lib/btstack/src/btstack_audio_generator.c")
    target_include_directories(speaking_clock_c PRIVATE
        "${_a2dp_test}"
        "${PICO_EXAMPLES_PATH}/bluetooth/config"
        "${PICO_SDK_PATH}/lib/btstack/3rd-party/hxcmod-player"
        "${PICO_SDK_PATH}/lib/btstack/3rd-party/hxcmod-player/mods")
    target_link_libraries(speaking_clock_c PRIVATE
        pico_cyw43_arch_none
        pico_btstack_cyw43
        pico_btstack_ble
        pico_btstack_classic
        pico_btstack_sbc_encoder)
    target_compile_definitions(speaking_clock_c PRIVATE CLOCK_ENABLE_A2DP=1)
    message(STATUS "Experimental A2DP enabled: test on spare Pico 2 W only")
endif()

# MP3 decoding is opt-in: keep the previously tested clock image unchanged
# unless the user explicitly requests an experiment. Third-party Helix source
# is checked out separately, not vendored into the clock repository.
option(CLOCK_ENABLE_MP3 "Experimental Helix MP3 diagnostics (no A2DP audio yet)" OFF)
set(CLOCK_HELIX_PATH "" CACHE PATH "Path to separate Helix-MP3-Decoder checkout")
if(CLOCK_ENABLE_MP3)
    if(NOT EXISTS "${CLOCK_HELIX_PATH}/src/helix_mp3.h" OR
       NOT EXISTS "${CLOCK_HELIX_PATH}/CMakeLists.txt")
        message(FATAL_ERROR "Set CLOCK_HELIX_PATH to a cloned Helix-MP3-Decoder checkout")
    endif()
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CLOCK_HELIX_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/helix_mp3_build")
    target_link_libraries(speaking_clock_c PRIVATE helix_mp3)
    target_compile_definitions(speaking_clock_c PRIVATE CLOCK_ENABLE_MP3=1)
    message(STATUS "Experimental MP3 decode-only probe enabled; audio routing unchanged")
endif()
