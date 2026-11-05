#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "clock_a2dp_control.h"

int btstack_main(int argc, const char *argv[]);

static btstack_timer_source_t serial_poll_timer;

static void print_commands(void) {
    printf("[CMD] p = play 300 ms tone, s = silence, i = info, h = help\n");
    fflush(stdout);
}

// Poll USB serial without blocking the Bluetooth event loop.
static void serial_poll_handler(btstack_timer_source_t *timer) {
    int ch;
    // Limit work per tick so a large serial input cannot starve A2DP.
    for (int i = 0; i < 16; i++) {
        ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) break;
        switch (ch) {
            case 'p':
            case 'P':
                if (!clock_a2dp_play_tone()) {
                    printf("[CMD] Cannot play: A2DP stream not active\n");
                }
                break;
            case 's':
            case 'S':
                clock_a2dp_stop_tone();
                break;
            case 'i':
            case 'I':
                clock_a2dp_print_status();
                break;
            case 'h':
            case 'H':
            case '?':
                print_commands();
                break;
            case '\r':
            case '\n':
                break;
            default:
                printf("[CMD] Unknown: %c\n", ch);
                print_commands();
                break;
        }
    }
    fflush(stdout);
    btstack_run_loop_set_timer(timer, 50);
    btstack_run_loop_add_timer(timer);
}

int main(void) {
    stdio_init_all();

    // Allow USB CDC enumeration before first boot messages.
    sleep_ms(3000);
    printf("\n[BOOT] nest_a2dp_autotest\n");
    printf("[BOOT] stdio initialized\n");
    fflush(stdout);

    printf("[BOOT] cyw43_arch_init...\n");
    fflush(stdout);
    int cyw43_status = cyw43_arch_init();
    printf("[BOOT] cyw43_arch_init returned %d\n", cyw43_status);
    fflush(stdout);
    if (cyw43_status != PICO_OK) {
        printf("[FAIL] cyw43 init failed\n");
        fflush(stdout);
        panic("cyw43 init failed");
    }

    printf("[BOOT] btstack_main...\n");
    fflush(stdout);
    int err = btstack_main(0, NULL);
    printf("[BOOT] btstack_main returned %d\n", err);
    fflush(stdout);
    if (err != 0) {
        panic("btstack_main failed");
    }

    btstack_run_loop_set_timer_handler(&serial_poll_timer, serial_poll_handler);
    btstack_run_loop_set_timer(&serial_poll_timer, 50);
    btstack_run_loop_add_timer(&serial_poll_timer);
    print_commands();
    printf("[BOOT] entering BTstack event loop\n");
    fflush(stdout);

    btstack_run_loop_execute();
    cyw43_arch_deinit();
    return 0;
}
