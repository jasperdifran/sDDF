#pragma once

#include <sys/types.h>

#define ETH_PID 1
#define NFS_PID 2
#define WEBSRV_PID 3
#define LWIP_PID 4
#define IDLE_PID 5
#define BENCH_PID 6
#define TIMER_PID 7

extern void *__sysinfo;

void syscalls_init(void);

typedef void (*socket_send)(void *buf, size_t len);
typedef size_t (*socket_recv)(void *buf, size_t len);