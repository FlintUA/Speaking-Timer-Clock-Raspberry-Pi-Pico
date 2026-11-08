#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { unsigned placeholder; } i2c_inst_t;
int i2c_write_blocking(i2c_inst_t *port, uint8_t address, const uint8_t *data,
                       size_t length, bool nostop);
