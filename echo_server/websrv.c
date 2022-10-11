#include <sel4cp.h>
#include <sel4/sel4.h>
#include <websrvint.h>
#include <string.h>

#include "shared_ringbuffer.h"
#include "echo.h"

#include "util.h"
// #include <util.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? -(a) : (a))

#define LWIP_CH 6

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
char tx_data[BUF_SIZE * 128] = {0};
unsigned int tx_len;

void init(void)
{
    ring_init(&rx_ring, (ring_buffer_t*)rx_websrv_avail, (ring_buffer_t*)rx_websrv_used, NULL, 0);
    ring_init(&tx_ring, (ring_buffer_t*)tx_websrv_avail, (ring_buffer_t*)tx_websrv_used, NULL, 0);
    sel4cp_dbg_puts("Init websrv pd\n");
}

void copy_mpybuf_to_ringbuf(void *cookie)
{
    /* Split response buf up into ring buf buffers */
    unsigned int bytes_written = 0;

    while (bytes_written < tx_len) {
        void *tx_cookie;
        uintptr_t tx_buf;
        unsigned int temp_len;
        
        int error = dequeue_avail(&tx_ring, &tx_buf, &temp_len, &tx_cookie);
        if (error) {
            sel4cp_dbg_puts("Failed to dequeue avail from tx_ring\n");
            return;
        }

        unsigned int bytes_to_write = MIN(tx_len - bytes_written, BUF_SIZE);
        for (unsigned int i = 0; i < bytes_to_write; i++) {
            ((char *)tx_buf)[i] = tx_data[bytes_written + i];
        }
        bytes_written += bytes_to_write;
        
        enqueue_used(&tx_ring, tx_buf, strlen((char *)tx_buf), cookie);
    }
}

void notified(sel4cp_channel ch)
{
    sel4cp_dbg_puts("Websrv notified\n");
    switch (ch)
    {
    case LWIP_CH:;
        /* Get request packet from lwip */
        uintptr_t rx_buf;
        void *rx_cookie;
        unsigned int rx_len;

        int error = dequeue_used(&rx_ring, &rx_buf, &rx_len, &rx_cookie);
        if (error) {
            sel4cp_dbg_puts("Failed to dequeue used from rx_ring\n");
            return;
        } else {
            sel4cp_dbg_puts("Dequeued used from rx_ring\n");
        }

        /* Init a response buf and process request */
        tx_len = 0;
        run_webserver((char *)rx_buf, (char *)tx_data, &tx_len);
        sel4cp_dbg_puts("Processed request\n");
        /* Copy response buf to ring buf */
        copy_mpybuf_to_ringbuf(rx_cookie);

        /* Release req buf */
        enqueue_avail(&rx_ring, rx_buf, BUF_SIZE, NULL);
        
        sel4cp_notify(LWIP_CH);
        break;
    default:
        sel4cp_dbg_puts("Unknown notf\n");
        break;
    }
}
