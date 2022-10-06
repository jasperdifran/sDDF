#include <sel4cp.h>
#include <sel4/sel4.h>
#include <websrvint.h>
#include <string.h>

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

ring_handle_t rx_ring;
ring_handle_t tx_ring;

void init(void)
{
    ring_init(&rx_ring, (ring_buffer_t*)rx_websrv_avail, (ring_buffer_t*)rx_websrv_used, NULL, 0);
    ring_init(&tx_ring, (ring_buffer_t*)tx_websrv_avail, (ring_buffer_t*)tx_websrv_used, NULL, 0);
    sel4cp_dbg_puts("Init websrv pd\n");
}

void notified(sel4cp_channel ch)
{
    switch (ch)
    {
    case LWIP_CH:;
        /* code */
        uintptr_t rx_buf;
        uintptr_t tx_buf;
        unsigned int rx_len;
        unsigned int tx_len;
        void *rx_cookie;
        void *tx_cookie;

        int error = dequeue_used(&rx_ring, &rx_buf, &rx_len, &rx_cookie);
        if (error) {
            sel4cp_dbg_puts("Failed to dequeue used from rx_ring\n");
            return;
        }

        error = dequeue_avail(&tx_ring, &tx_buf, &tx_len, &tx_cookie);
        if (error) {
            sel4cp_dbg_puts("Failed to dequeue avail from tx_ring\n");
            return;
        }

        run_webserver((char *)rx_buf, (char *)tx_buf);
        
        enqueue_used(&tx_ring, tx_buf, strlen((char *)tx_buf), rx_cookie);
        enqueue_avail(&rx_ring, rx_buf, BUF_SIZE, NULL);
        
        sel4cp_notify(LWIP_CH);
        break;
    default:
        sel4cp_dbg_puts("Unknown notf\n");
        break;
    }
}
