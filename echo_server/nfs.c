#include <sel4cp.h>
// #include <sel4/sel4.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>
#include <sys/types.h>
#include <syscall_implementation.h>

#include "shared_ringbuffer.h"

#include "util.h"

#define WEBSRV_CH 7
#define NFS_CH 8

#define NUM_BUFFERS 512
#define BUF_SIZE 2048

pid_t my_pid = NFS_PID;

struct rpc_context *rpc;

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

void nfs_connect_cb(struct rpc_context *rpc, int status, void *data, void *private_data)
{
    if (status != RPC_STATUS_SUCCESS)
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

void init_post(void)
{
    rpc = rpc_init_context();

    if (rpc == NULL)
    {
        sel4cp_dbg_puts("init: failed to init nfs context\n");
        return;
    }

    if (rpc_connect_async(rpc, "nfshomes.keg.cse.unsw.edu.au", 2049, nfs_connect_cb, NULL) != 0)
    {
        sel4cp_dbg_puts("init: failed to connect to nfs server\n");
        return;
    }
    sel4cp_dbg_puts("init: connected to nfs server\n");

    sel4cp_dbg_puts("Init nfs pd\n");
}

void notified(sel4cp_channel ch)
{
    sel4cp_dbg_puts("NFS notified\n");
    switch (ch)
    {
    case WEBSRV_CH:
        sel4cp_dbg_puts("Got notification from websrv\n");
        // Requesting a file
        break;

    case NFS_CH:
        sel4cp_dbg_puts("Got notification from nfs\n");
        // Got a file
        break;
    default:
        sel4cp_dbg_puts("Got notification from unknown channel\n");
        break;
    }
}