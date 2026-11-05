#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"

int btstack_main(int argc, const char *argv[]);

int main(void) {
    stdio_init_all();

    if (cyw43_arch_init() != PICO_OK) {
        panic("cyw43 init failed");
    }

    sleep_ms(1200);
    printf("\nNest A2DP auto-test starting\n");
    printf("Expected behavior: scan automatically, connect to an audio speaker, stream sine\n");

    btstack_main(0, NULL);
    btstack_run_loop_execute();

    cyw43_arch_deinit();
    return 0;
}
