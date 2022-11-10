#include <sel4cp.h>
#include <sel4/sel4.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <nfsc/libnfs.h>
#include <sys/types.h>
#include <syscall_implementation.h>

#include "util.h"

#define WEBSRV_CH 7

pid_t my_pid = NFS_PID;

struct nfs_context *nfsContext = NULL;

void nfs_mount_cb(int err, struct nfs_context *nfs, void *data, void *private_data)
{
    if (err != 0)
    {
        sel4cp_dbg_puts("nfs_mount_cb: failed to mount nfs share\n");
        return;
    }
    sel4cp_dbg_puts("nfs_mount_cb: nfs share mounted\n");
}

void init(void)
{
    sel4cp_dbg_puts("init: starting nfs client\n");
    syscalls_init();
    char *ptr = malloc(5);
    sel4cp_dbg_puts("init: malloced ptr\n");
    strcpy(ptr, "hi");
    sel4cp_dbg_puts(ptr);

    nfsContext = nfs_init_context();
    return;
    if (nfsContext == NULL)
    {
        sel4cp_dbg_puts("Failed to init nfs context\n");
        return;
    }
    // Mount nfs directory

    nfs_mount_async(nfsContext, "somesever", "/some/path", nfs_mount_cb, NULL);

    sel4cp_dbg_puts("Init websrv pd\n");
}

void notified(sel4cp_channel ch)
{
    sel4cp_dbg_puts("NFS notified\n");
    switch (ch)
    {
    case WEBSRV_CH:
        sel4cp_dbg_puts("Got notification from websrv\n");
        break;
    default:
        sel4cp_dbg_puts("Got notification from unknown channel\n");
        break;
    }
}