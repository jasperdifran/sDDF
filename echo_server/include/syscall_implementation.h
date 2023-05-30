#pragma once

#include <sys/types.h>

#define ETH_PID 1
#define NFS_PID 2
#define WEBSRV_PID 3
#define LWIP_PID 4
#define IDLE_PID 5
#define BENCH_PID 6
#define TIMER_PID 7

/* Commands used between the websrv and the nfs pds */
#define SYS_ERROR 1
#define SYS_STAT64 2
#define SYS_OPENREADCLOSE 3

/* Error codes to be sent from nfs pd to websrv pd */
#define INTERNAL_SERVER_ERROR 500

/* Flags for websrv to lwip to change the behvaiour of commands */
#define FILE_RESPONSE 0b00000001


extern void *__sysinfo;

void syscalls_init(void);

typedef void (*socket_send_t)(int fd, void *buf, size_t len);
typedef size_t (*socket_recv_t)(int fd, void *buf, size_t len);
typedef void (*socket_close_t)(int fd);

static inline void write_red(char *s)
{
    sel4cp_dbg_puts("\033[31m");
    sel4cp_dbg_puts(s);
    sel4cp_dbg_puts("\033[0m");
}