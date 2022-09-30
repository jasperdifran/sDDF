#include <sel4cp.h>
#include <sel4/sel4.h>
#include <websrvint.h>

#include "util.h"

#define LWIP_CH 3

uint64_t uart_base;

void init(void)
{
    sel4cp_dbg_puts("Init websrv pd\n");    
}

void notified(sel4cp_channel ch)
{
    sel4cp_dbg_puts("Notif\n");
    switch (ch)
    {
    case LWIP_CH:
        /* code */
        sel4cp_dbg_puts("LWIP sent notf\n");
        run_webserver();
        break;
    default:
        sel4cp_dbg_puts("Unknown notf\n");
        break;
    }
}
