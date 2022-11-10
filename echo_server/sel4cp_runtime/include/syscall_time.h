#pragma once

#include <stdint.h>

uint64_t rtc_now();
uint64_t rtc_now_ms();
int rtc_enable(void);