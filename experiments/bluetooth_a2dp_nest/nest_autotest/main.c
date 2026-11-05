#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"

int btstack_main(int argc, const char *argv[]);

int main(void) {
    stdio_init_all();

    // Give USB CDC time to enumerate so early diagnostics are visible.
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
    btstack_main(0, NULL);
    printf("[BOOT] btstack_main returned, entering run loop\n");
    fflush(stdout);

    btstack_run_loop_execute();

    printf("[BOOT] run loop exited\n");
    fflush(stdout);
    cyw43_arch_deinit();
    return 0;
}
