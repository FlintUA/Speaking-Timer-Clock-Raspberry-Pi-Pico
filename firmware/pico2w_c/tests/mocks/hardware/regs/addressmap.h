#pragma once
#include <stdint.h>
extern uint8_t test_flash[];
#define XIP_BASE ((uintptr_t)test_flash)
