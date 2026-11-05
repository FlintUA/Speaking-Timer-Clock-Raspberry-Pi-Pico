#include <stdio.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();

    while (true) {
        printf("USB STDIO TEST OK\n");
        fflush(stdout);
        sleep_ms(1000);
    }
}
