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

// DFPlayer Mini feedback (0x4E): number of playable files in a folder.
// The RX line must be connected. Some clones don't implement this query.
// Time-limited polling is intentionally confined to opening the music UI.
uint8_t dfplayer_folder_count(uint8_t folder, uint64_t now_ms) {
    if (folder < 1u || folder > 99u) return 0u;
    for (unsigned attempt = 0; attempt < 3u; ++attempt) {
        while (uart_is_readable(DFPLAYER_UART)) (void)uart_getc(DFPLAYER_UART);
        const uint8_t cmd = 0x4eu, ack = 1u;
        uint16_t sum = (uint16_t)(0xffu + 0x06u + cmd + ack + folder);
        uint16_t checksum = (uint16_t)(0u - sum);
        uint8_t request[10] = {
            0x7eu, 0xffu, 0x06u, cmd, ack, 0u, folder,
            (uint8_t)(checksum >> 8), (uint8_t)checksum, 0xefu
        };
        uart_write_blocking(DFPLAYER_UART, request, sizeof(request));
        last_command_ms = to_ms_since_boot(get_absolute_time());
        has_sent_command = true;

        uint8_t frame[10] = {0};
        unsigned size = 0;
        uint64_t deadline = to_ms_since_boot(get_absolute_time()) + 700u;
        while (to_ms_since_boot(get_absolute_time()) < deadline) {
            if (!uart_is_readable(DFPLAYER_UART)) {
                sleep_ms(5);
                continue;
            }
            uint8_t b = (uint8_t)uart_getc(DFPLAYER_UART);
            if (size == 0u && b != 0x7eu) continue;
            frame[size++] = b;
            if (size < 10u) continue;
            if (frame[0] == 0x7eu && frame[1] == 0xffu &&
                frame[2] == 0x06u && frame[3] == 0x4eu &&
                frame[9] == 0xefu) {
                uint16_t expected = (uint16_t)(0u -
                    (0xffu + 0x06u + frame[3] + frame[4] + frame[5] + frame[6]));
                uint16_t received = (uint16_t)(((uint16_t)frame[7] << 8) | frame[8]);
                uint16_t files = (uint16_t)(((uint16_t)frame[5] << 8) | frame[6]);
                if (expected == received && files >= 1u && files <= 255u)
                    return (uint8_t)files;
            }
            // Resync on the next start byte even when feedback has noise.
            size = 0u;
            if (b == 0x7eu) { frame[0] = b; size = 1u; }
        }
        sleep_ms(150);
    }
    (void)now_ms;
    return 0u;
}

