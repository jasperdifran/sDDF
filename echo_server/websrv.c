#include <sel4cp.h>
#include <sel4/sel4.h>
#include <websrvint.h>
#include <string.h>

#include <syscall_implementation.h>

#include "shared_ringbuffer.h"
#include "echo.h"

#include "util.h"

#define MIN(a, b) (a < b) ? a : b
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? -(a) : (a))

#define LWIP_CH 6
#define NFS_CH 7

#define ETHER_MTU 1500
#define NUM_BUFFERS 512
#define BUF_SIZE 2048
#define MAX_REQUEST_CONTS 10

int req_ids = 0;

typedef struct
{
    int id;
    int used;
    void *socket_id;
} request_cont_t;

request_cont_t request_conts[MAX_REQUEST_CONTS] = {0};

pid_t my_pid = WEBSRV_PID;

uintptr_t rx_websrv_avail;
uintptr_t rx_websrv_used;
uintptr_t tx_websrv_avail;
uintptr_t tx_websrv_used;

uintptr_t shared_websrv_lwip_vaddr;

uintptr_t rx_nfs_websrv_avail;
uintptr_t rx_nfs_websrv_used;
uintptr_t tx_nfs_websrv_avail;
uintptr_t tx_nfs_websrv_used;

uintptr_t shared_nfs_websrv_vaddr;

ring_handle_t lwip_rx_ring;
ring_handle_t lwip_tx_ring;
ring_handle_t nfs_rx_ring;
ring_handle_t nfs_tx_ring;

char tx_data[BUF_SIZE * 256] = {0};
unsigned int tx_len;

void *cont_data_store = NULL;
int cont_done = 0;

void init(void)
{
    syscalls_init();
    ring_init(&lwip_rx_ring, (ring_buffer_t *)rx_websrv_avail, (ring_buffer_t *)rx_websrv_used, NULL, 0);
    ring_init(&lwip_tx_ring, (ring_buffer_t *)tx_websrv_avail, (ring_buffer_t *)tx_websrv_used, NULL, 0);
    init_websrv();
    ring_init(&nfs_rx_ring, (ring_buffer_t *)rx_nfs_websrv_avail, (ring_buffer_t *)rx_nfs_websrv_used, NULL, 0);
    ring_init(&nfs_tx_ring, (ring_buffer_t *)tx_nfs_websrv_avail, (ring_buffer_t *)tx_nfs_websrv_used, NULL, 0);
    sel4cp_dbg_puts("Init websrv pd\n");

    for (int i = 0; i < MAX_REQUEST_CONTS; i++)
    {
        request_conts[i].id = i;
        request_conts[i].used = 0;
        request_conts[i].socket_id = NULL;
    }
}

void printnum(int num)
{
    char buf[10];
    int i = 0;
    if (num == 0)
    {
        sel4cp_dbg_putc('0');
        return;
    }
    while (num > 0)
    {
        buf[i] = num % 10 + '0';
        num /= 10;
        i++;
    }
    // reverse buf
    for (int j = 0; j < i / 2; j++)
    {
        char tmp = buf[j];
        buf[j] = buf[i - j - 1];
        buf[i - j - 1] = tmp;
    }
    buf[i] = '\0';
    sel4cp_dbg_puts(buf);
}
void label_num(char *s, int n)
{
    sel4cp_dbg_puts(s);
    printnum(n);
    sel4cp_dbg_puts("\n");
}

void copy_mpybuf_to_ringbuf(void *cookie)
{
    /* Split response buf up into ring buf buffers */
    unsigned int bytes_written = 0;

    while (bytes_written < tx_len)
    {
        // sel4cp_dbg_puts("Copying mpybuf to ringbuf");
        // label_num("bytes_written: ", bytes_written);
        // label_num("tx_len: ", tx_len);
        void *tx_cookie;
        uintptr_t tx_buf;
        unsigned int temp_len;

        if (ring_empty(lwip_tx_ring.avail_ring))
        {
            sel4cp_notify(LWIP_CH);
        }
        // while (ring_empty(&lwip_tx_ring));
        int error = dequeue_avail(&lwip_tx_ring, &tx_buf, &temp_len, &tx_cookie);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue avail from lwip_tx_ring\n");
            return;
        }

        unsigned int bytes_to_write = MIN((tx_len - bytes_written), BUF_SIZE);
        for (unsigned int i = 0; i < bytes_to_write; i++)
        {
            ((char *)tx_buf)[i] = tx_data[bytes_written + i];
        }
        bytes_written += bytes_to_write;

        enqueue_used(&lwip_tx_ring, tx_buf, bytes_to_write, cookie);
    }
}

void req_file(char *filename)
{
    sel4cp_dbg_puts("Requesting file: ");
    sel4cp_dbg_puts(filename);
    sel4cp_dbg_puts("\n");
    sel4cp_notify(NFS_CH);
}

void stat_file(char *filename)
{
    void *continuation_id;
    uintptr_t tx_buf;
    unsigned int buf_len;

    dequeue_avail(&nfs_tx_ring, &tx_buf, &buf_len, &continuation_id);
    int pathLen = strlen(filename);
    char *buf = (char *)tx_buf;
    buf[0] = SYS_STAT64;
    memcpy(buf + 1, filename, pathLen);
    buf[pathLen + 1] = '\0';
    enqueue_used(&nfs_tx_ring, tx_buf, pathLen + 1, continuation_id);

    sel4cp_notify(NFS_CH);
}

void notified(sel4cp_channel ch)
{
    switch (ch)
    {
    case LWIP_CH:;
        /* Incoming new request packet from lwip */
        while (!ring_empty(lwip_rx_ring.used_ring))
        {
            uintptr_t rx_buf;
            void *rx_cookie;
            unsigned int rx_len;

            int error = dequeue_used(&lwip_rx_ring, &rx_buf, &rx_len, &rx_cookie);
            if (error)
            {
                sel4cp_dbg_puts("Failed to dequeue used from lwip_rx_ring\n");
                return;
            }

            /* Init a response buf and process request */
            tx_len = 0;

            // Find a free continuation to use. For now, assuming there is one free
            // and we know that pretty much every request will be async.
            int contInd = 0;
            while (request_conts[contInd].used)
                contInd++;
            request_cont_t *cont = &request_conts[contInd];
            cont->used = 1;
            cont->socket_id = rx_cookie;

            // We know that pretty much every request will be async
            run_webserver((char *)rx_buf, (char *)tx_data, &tx_len);

            if (cont_done)
            {
                sel4cp_dbg_puts("Continuation done\n");
                cont_done = 0;
                cont_data_store = NULL;
                copy_mpybuf_to_ringbuf(rx_cookie);
                sel4cp_notify(LWIP_CH);

                cont->used = 0;
                cont->socket_id = NULL;
            }

            /* Copy response buf to ring buf */

            /* Release req buf */
            enqueue_avail(&lwip_rx_ring, rx_buf, BUF_SIZE, NULL);
        }
        break;
    case NFS_CH:;
        /* Continuation of a request */
        sel4cp_dbg_puts("Websrv got a reponse from NFS CH\n");
        break;

    default:
        sel4cp_dbg_puts("Unknown notif\n");
        break;
    }
}
