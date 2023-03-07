#include <timer.h>
#include <sel4cp.h>
#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

#define TIMER_CH 9

u32_t sys_now(void)
{
    sel4cp_msginfo msginfo = sel4cp_msginfo_new(0, 1);
    sel4cp_mr_set(0, SYS_NOW);
    sel4cp_msginfo ret = sel4cp_ppcall(TIMER_CH, msginfo);
    uint32_t now = sel4cp_mr_get(0);
    return now;
}
