#include <sel4cp.h>
#include <sel4/sel4.h>
#include <websrvint.h>

#include "util.h"

uint64_t uart_base;

void init(void)
{
    sel4cp_dbg_puts("Running web srv\n");
    run_webserver();
    
}

void notified(sel4cp_channel ch)
{
    sel4cp_dbg_puts("Notif\n");
}
