#pragma once
#define PICO_OK 0
int flash_safe_execute(void (*callback)(void *), void *arg, unsigned timeout);
