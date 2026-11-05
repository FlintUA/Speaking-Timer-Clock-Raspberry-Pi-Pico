#include "dfplayer.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define DFPLAYER_UART uart0
#define DFPLAYER_TX 16
#define DFPLAYER_RX 17
#define DFPLAYER_BUSY 18

static uint64_t last_command_ms;
static bool has_sent_command;

void dfplayer_init(void) {
    uart_init(DFPLAYER_UART, 9600);
    gpio_set_function(DFPLAYER_TX, GPIO_FUNC_UART);
    gpio_set_function(DFPLAYER_RX, GPIO_FUNC_UART);
    gpio_init(DFPLAYER_BUSY);
    gpio_set_dir(DFPLAYER_BUSY, GPIO_IN);
    gpio_pull_up(DFPLAYER_BUSY);
    has_sent_command = false;
}

bool dfplayer_busy(void) {
    return gpio_get(DFPLAYER_BUSY) == 0;
}

bool dfplayer_command_ready(uint64_t now_ms) {
    return !has_sent_command || now_ms - last_command_ms >= 120u;
}

static void send_command(uint8_t cmd, uint8_t p1, uint8_t p2, uint64_t now_ms) {
    // Same 10-byte packet and two's-complement checksum as audio.py.
    uint16_t sum = (uint16_t)(0xffu + 0x06u + cmd + p1 + p2);
    uint16_t checksum = (uint16_t)(0u - sum);
    uint8_t frame[10] = {
        0x7e, 0xff, 0x06, cmd, 0x00, p1, p2,
        (uint8_t)(checksum >> 8), (uint8_t)checksum, 0xef
    };
    uart_write_blocking(DFPLAYER_UART, frame, sizeof(frame));
    last_command_ms = now_ms;
    has_sent_command = true;
}

void dfplayer_play_folder_track(uint8_t folder, uint8_t track, uint64_t now_ms) {
    send_command(0x0f, folder, track, now_ms);
}

void dfplayer_set_volume(uint8_t volume, uint64_t now_ms) {
    if (volume > 30) volume = 30;
    send_command(0x06, 0, volume, now_ms);
}

void dfplayer_pause(uint64_t now_ms) {
    send_command(0x0e, 0, 0, now_ms);
}
