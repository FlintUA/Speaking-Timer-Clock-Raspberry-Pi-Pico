cmake_minimum_required(VERSION 3.13)

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(clock_phone_ap C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(WIFI_EXAMPLES "${PICO_EXAMPLES_PATH}/pico_w/wifi")
set(DHCP_DIR "${WIFI_EXAMPLES}/access_point/dhcpserver")
if(NOT EXISTS "${DHCP_DIR}/dhcpserver.c")
    message(FATAL_ERROR "Set PICO_EXAMPLES_PATH to a Raspberry Pi pico-examples checkout.")
endif()

pico_add_library(clock_phone_content NOFLAG)
pico_set_lwip_httpd_content(clock_phone_content INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/content/index.html"
)

add_executable(clock_phone_ap main.c "${DHCP_DIR}/dhcpserver.c")
target_include_directories(clock_phone_ap PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}" "${WIFI_EXAMPLES}" "${DHCP_DIR}"
)
target_link_libraries(clock_phone_ap PRIVATE
    pico_stdlib pico_rand pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http clock_phone_content
)
pico_configure_ip4_address(clock_phone_ap PRIVATE
    CYW43_DEFAULT_IP_AP_ADDRESS 192.168.4.1
)
pico_enable_stdio_usb(clock_phone_ap 1)
pico_enable_stdio_uart(clock_phone_ap 0)
pico_add_extra_outputs(clock_phone_ap)
