#include <sel4cp.h>
#include <sel4/sel4.h>
#include <websrvint.h>

#include "util.h"

#define LWIP_CH 6

uint64_t uart_base;

void init(void)
{
    sel4cp_dbg_puts("Init websrv pd\n");    
}

// char req_in[50] = "Some packing going in";
char buffawuffa[1024] = "GET / HTTP/1.1\r\nHost: www.tutorialspoint.com\r\nAccept-Language: en-us\r\n\r\n";

void notified(sel4cp_channel ch)
{
    sel4cp_dbg_puts("Notif\n");
    switch (ch)
    {
    case LWIP_CH:
        /* code */
        run_webserver((char *)buffawuffa);
        sel4cp_notify(LWIP_CH);
        break;
    default:
        sel4cp_dbg_puts("Unknown notf\n");
        break;
    }
}
