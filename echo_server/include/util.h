/*
 * Copyright 2022, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#define UART_REG(x) ((volatile uint32_t *)(UART_BASE + (x)))
#define UART_BASE 0x5000000 // 0x30890000 in hardware on imx8mm.
#define STAT 0x98
#define TRANSMIT 0x40
#define STAT_TDRE (1 << 14)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#ifdef __GNUC__
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (!!(x))
#define unlikely(x) (!!(x))
#endif

#define MASK(x) ((1 << (x)) - 1)
#define BIT(x) (1 << (x))
#define MIN(a, b) (a < b) ? a : b
#define MAX(a, b) (a > b) ? a : b
#define ABS(a) (a < 0) ? -a : a

static inline void
putC(uint8_t ch)
{
    while (!(*UART_REG(STAT) & STAT_TDRE))
        ;
    *UART_REG(TRANSMIT) = ch;
}

static inline void
print(const char *s)
{
    while (*s)
    {
        putC(*s);
        s++;
    }
}

static inline char
hexchar(unsigned int v)
{
    return v < 10 ? '0' + v : ('a' - 10) + v;
}

static inline void
puthex64(uint64_t val)
{
    char buffer[16 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[16 + 3 - 1] = 0;
    for (unsigned i = 16 + 1; i > 1; i--)
    {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    print(buffer);
}

static inline void split_int_to_buf(int num, char *buf)
{
    buf[0] = (num >> 24) & 0xFF;
    buf[1] = (num >> 16) & 0xFF;
    buf[2] = (num >> 8) & 0xFF;
    buf[3] = num & 0xFF;
}

static inline void split_ptr_to_buf(void *ptr, char *buf)
{
    uintptr_t num = (uintptr_t)ptr;

    for (int i = 0; i < sizeof(void *); i++)
    {
        buf[i] = (num >> (8 * (sizeof(void *) - i - 1))) & 0xFF;
    }
}

static inline void *get_ptr_from_buf(char *buf, int offset)
{
    uintptr_t ret = 0;

    for (int i = 0; i < sizeof(void *); i++)
    {
        ret |= (buf)[offset + i] << (8 * (sizeof(void *) - i - 1));
    }
    return (void *)ret;
}

static inline int get_int_from_buf(char *buf, int offset)
{
    int ret = 0;
    ret |= (buf)[offset + 0] << 24;
    ret |= (buf)[offset + 1] << 16;
    ret |= (buf)[offset + 2] << 8;
    ret |= (buf)[offset + 3] << 0;
    return ret;
}