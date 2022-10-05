#include <sel4cp.h>
#include <sel4/sel4.h>
#include <websrvint.h>

#include "shared_ringbuffer.h"
#include "echo.h"

#include "util.h"

#define LWIP_CH 6

#define LINK_SPEED 1000000000 // Gigabit
#define ETHER_MTU 1500
#define NUM_BUFFERS 512
#define BUF_SIZE 2048

uint64_t uart_base;

uintptr_t rx_websrv_avail;
uintptr_t rx_websrv_used;
uintptr_t tx_websrv_avail;
uintptr_t tx_websrv_used;

uintptr_t shared_websrv_lwip_vaddr;

typedef struct state {
    /* mac address for this client */
    uint8_t mac[6];

    /* Pointers to shared buffers */
    ring_handle_t rx_ring;
    ring_handle_t tx_ring;
} state_t;

state_t state;


void init(void)
{
    ring_init(&state.rx_ring, (ring_buffer_t*)rx_websrv_avail, (ring_buffer_t*)rx_websrv_used, NULL, 1);
    ring_init(&state.tx_ring, (ring_buffer_t*)tx_websrv_avail, (ring_buffer_t*)tx_websrv_used, NULL, 1);
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
