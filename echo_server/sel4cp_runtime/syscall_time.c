/**
 * @file time_core.c
 * @author Jasper Di Francesco (jasper.difrancesco@gmail.com)
 * @brief imx8mm RTC time functionality
 * @version 0.1
 * @date 2022-10-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <syscall_time.h>

#define HPCOMR 0x04
#define HPCR 0x08
#define HPSR 0x14
#define HPRTCMR 0x24
#define HPRTCLR 0x28
#define LPLR 0x34
#define LPCR 0x38
#define LPSR 0x4C
#define LPSMCMR 0x5C
#define LPSMCLR 0x60

#define RTC_EN 0
#define MC_ENV 2
#define HPCALB_EN 8

#define BIT(n) (1 << (n))
#define MASK(n) ((1 << (n)) - 1)

uint64_t snvs_base;
#define RTC_REG(x) ((volatile uint32_t *)(snvs_base + (x)))

uint64_t rtc_now()
{
    uint32_t high = *RTC_REG(HPRTCMR) & MASK(15);
    uint32_t low = *RTC_REG(HPRTCLR);
    uint64_t now = ((uint64_t)high << 32) | low;
    return now;
}

uint64_t rtc_now_ms()
{
    uint64_t now = rtc_now();
    return (now * 1000) >> 15;
}

int rtc_enable(void)
{
    *RTC_REG(HPCR) |= BIT(HPCALB_EN);
    *RTC_REG(HPCR) |= BIT(RTC_EN);
    return (*RTC_REG(HPCR) & BIT(RTC_EN));
}

void rtc_stat(void)
{
    printf("SNVS HPCOMR: %x\n", *RTC_REG(HPCOMR));
    printf("SNVS HPCR: %x\n", *RTC_REG(HPCR));
    printf("SNVS HPSR: %x\n", *RTC_REG(HPSR));
    printf("SNVS HPRTCMR: %x\n", *RTC_REG(HPRTCMR));
    printf("SNVS HPRTCLR: %x\n", *RTC_REG(HPRTCLR));
    printf("SNVS LPLR: %x\n", *RTC_REG(LPLR));
    printf("SNVS LPCR: %x\n", *RTC_REG(LPCR));
    printf("SNVS LPSR: %x\n", *RTC_REG(LPSR));
    printf("SNVS LPSMCMR: %x\n", *RTC_REG(LPSMCMR));
    printf("SNVS LPSMCLR: %x\n", *RTC_REG(LPSMCLR));
}
