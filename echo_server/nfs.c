#include <sel4cp.h>
#include <sel4/sel4.h>
#include <string.h>
#include <sys/time.h>
#include <nfsc/libnfs.h>

#include "util.h"

#define WEBSRV_CH 7

struct nfs_context *nfsContext = NULL;

extern void *__sysinfo;

long sel4_vsyscall(long sysnum, ...)
{
    sel4cp_dbg_puts("sel4_vsyscall: \n");
    return 0;
}

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
    sel4cp_dbg_puts("Init nfs pd\n");
    __sysinfo = sel4_vsyscall;

    sel4cp_dbg_puts("init NFS: starting\n");
    nanosleep((struct timespec[]){{1, 0}}, NULL);
    sel4cp_dbg_puts("init NFS: nanosleep done\n");

    nfsContext = nfs_init_context();
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