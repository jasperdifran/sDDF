#include <sel4cp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>
#include <sys/types.h>
#include <syscall_implementation.h>
#include <poll.h>

#include "shared_ringbuffer.h"

#include "util.h"

#define WEBSRV_CH 7
#define LWIP_NFS_CH 8
#define TIMER_CH 10

#define NUM_BUFFERS 512
#define BUF_SIZE 2048

#define SERVER "10.1.1.27"
#define EXPORT "/VIRTUAL"
#define NFSFILE "/BOOKS/Classics/Dracula.djvu"
#define NFSDIR "/BOOKS/Classics/"

pid_t my_pid = NFS_PID;

struct client
{
    char server[128];
    char export[128];
    uint32_t mount_port;
    struct nfsfh *nfsfh;
    int is_finished;
};

struct rpc_context *rpc;
struct nfs_context *nfs;
struct client client = {
    .server = SERVER,
    .export = EXPORT,
    .mount_port = 0,
    .nfsfh = NULL,
    .is_finished = 0,
};

struct pollfd pfds[2]; /* nfs:0  mount:1 */

// struct nfs_context *nfsContext = NULL;

// void nfs_mount_cb(int err, struct nfs_context *nfs, void *data, void *private_data)
// {
//     if (err != 0)
//     {
//         sel4cp_dbg_puts("nfs_mount_cb: failed to mount nfs share\n");
//         return;
//     }
//     sel4cp_dbg_puts("nfs_mount_cb: nfs share mounted\n");
// }

void write_bright_green(const char *str)
{
    sel4cp_dbg_puts("\033[32m");
    sel4cp_dbg_puts(str);
    sel4cp_dbg_puts("\033[0m");
}

void nfs_connect_cb(int err, struct nfs_context *nfs_ctx, void *data, void *private_data)
{
    if (err != 0)
    {
        sel4cp_dbg_puts("nfs_connect_cb: failed to connect to nfs server\n");
        return;
    }
    sel4cp_dbg_puts("nfs_connect_cb: connected to nfs server\n");
}

uintptr_t rx_nfs_avail;
uintptr_t rx_nfs_used;
uintptr_t tx_nfs_avail;
uintptr_t tx_nfs_used;

uintptr_t shared_nfs_lwip_vaddr;

uintptr_t rx_nfs_websrv_avail;
uintptr_t rx_nfs_websrv_used;
uintptr_t tx_nfs_websrv_avail;
uintptr_t tx_nfs_websrv_used;

uintptr_t shared_nfs_websrv_vaddr;

ring_handle_t lwip_rx_ring;
ring_handle_t lwip_tx_ring;
ring_handle_t websrv_rx_ring;
ring_handle_t websrv_tx_ring;

bool nfs_init = false;
bool nfs_socket_connected = false;

void init(void)
{
    sel4cp_dbg_puts("init: starting nfs client\n");
    syscalls_init();

    ring_init(&lwip_rx_ring, (ring_buffer_t *)rx_nfs_avail, (ring_buffer_t *)rx_nfs_used, NULL, 0);
    ring_init(&lwip_tx_ring, (ring_buffer_t *)tx_nfs_avail, (ring_buffer_t *)tx_nfs_used, NULL, 0);

    ring_init(&websrv_rx_ring, (ring_buffer_t *)rx_nfs_websrv_avail, (ring_buffer_t *)rx_nfs_websrv_used, NULL, 1);
    ring_init(&websrv_tx_ring, (ring_buffer_t *)tx_nfs_websrv_avail, (ring_buffer_t *)tx_nfs_websrv_used, NULL, 1);

    for (int i = 0; i < NUM_BUFFERS - 1; i++)
    {
        enqueue_avail(&websrv_rx_ring, shared_nfs_websrv_vaddr + (i * BUF_SIZE), BUF_SIZE, NULL);
    }

    for (int i = 0; i < NUM_BUFFERS - 1; i++)
    {
        enqueue_avail(&websrv_tx_ring, shared_nfs_websrv_vaddr + (BUF_SIZE * (i * NUM_BUFFERS)), BUF_SIZE, NULL);
    }
}

void write_pointer_hex(void *ptr);

void init_post(void)
{
    sel4cp_dbg_puts("init_post: starting nfs client\n");
    nfs = nfs_init_context();

    if (nfs == NULL)
    {
        sel4cp_dbg_puts("init: failed to init nfs context\n");
        return;
    }

    if (nfs_mount_async(nfs, client.server, client.export, nfs_connect_cb, &client) != 0)
    {
        sel4cp_dbg_puts("init: failed to connect to nfs server\n");
        return;
    }
    sel4cp_dbg_puts("init: connected to nfs server\n");

    sel4cp_dbg_puts("Init nfs pd\n");

    write_pointer_hex(nfs_connect_cb);
}

void notified(sel4cp_channel ch)
{
    switch (ch)
    {
    case LWIP_NFS_CH:
        if (!nfs_init)
        {
            init_post();
            nfs_init = true;
        }
        else if (!nfs_socket_connected)
        {
            write_bright_green("NFS socket connected\n");
            nfs_service(nfs, POLLOUT);
            nfs_socket_connected = true;
        }
        else
        {
            write_bright_green("Processing a packet...\n");
        }
        break;
    case WEBSRV_CH:
        sel4cp_dbg_puts("Got notification from websrv\n");
        // Requesting a file
        break;
    case TIMER_CH:
        nfs_service(nfs, 0);
        break;
    default:
        sel4cp_dbg_puts("Got notification from unknown channel\n");
        break;
    }
}