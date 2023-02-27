/**
 * @file lwip_nfs_socket.c
 * @author Jasper Di Francesco (jasper.difrancesc@gmail.com)
 * @brief Provides a socket interface for the NFS server. It exists in the LWIP PD.
 * @version 0.1
 * @date 2022-12-17
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <sel4cp.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>

#include <lwip_nfs_socket.h>

#include "lwip/ip.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "echo.h"
#include "shared_ringbuffer.h"
#include "util.h"

#define LWIP_NFS_CH 8

// Stores listening socket tcp_ptcb
static struct tcp_pcb *tcp_socket;
struct tcp_pcb *sockets[10];
int socket_count = 0;

extern nfs_state_t nfs_state;

void write_cyan(const char *str)
{
    sel4cp_dbg_puts("\033[36m");
    sel4cp_dbg_puts(str);
    sel4cp_dbg_puts("\033[0m");
}

int create_socket(void)
{
    write_cyan("Creating socket, early ret\n");
    nfs_socket_create();
    return 1;
}

int fcntl(void)
{
    int fd = sel4cp_mr_get(1);
    int cmd = sel4cp_mr_get(2);
    int arg = sel4cp_mr_get(3);
    sel4cp_dbg_puts("fcntl\n");
    labelnum("fd: ", fd);
    labelnum("cmd: ", cmd);
    labelnum("arg: ", arg);
    return 0;
}

static err_t nfs_socket_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (p == NULL)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    uintptr_t data;
    unsigned int len;
    void *cookie;

    int error = dequeue_avail(&nfs_state.rx_ring, &data, &len, &cookie);
    if (error)
    {
        sel4cp_dbg_puts("Failed to dequeue avail from rx_ring\n");
        return ERR_OK;
    }

    pbuf_copy_partial(p, (void *)data, p->tot_len, 0);

    cookie = (void *)tpcb;

    enqueue_used(&nfs_state.rx_ring, data, p->tot_len, cookie);

    sel4cp_notify(WEBSRV_CH);
    tcp_recved(tpcb, p->tot_len);
    return ERR_OK;
}

static err_t nfs_socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    return ERR_OK;
}

// Connected function
err_t nfs_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    write_cyan("Successfully connected to NFS server!\n");
    tcp_sent(tpcb, nfs_socket_sent_callback);
    tcp_recv(tpcb, nfs_socket_recv_callback);
    sel4cp_notify(LWIP_NFS_CH);
    return ERR_OK;
}

void err_func(void *arg, err_t err)
{
    write_cyan("Error connecting to NFS server!\n");
}

int socket_connect()
{
    write_cyan("LWIP connecting nfs...\n");

    nfs_socket_connect("10.13.0.11");
}

int nfs_socket_create(void)
{
    tcp_socket = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (tcp_socket == NULL)
    {
        write_cyan("Error creating socket\n");
        return 1;
    }
    tcp_err(tcp_socket, err_func);
    return 0;
}

int nfs_socket_connect(char *addr)
{
    ip_addr_t ipaddr;
    int port = 111;
    ip4_addr_set_u32(&ipaddr, ipaddr_addr("10.13.0.11"));

    err_t error = tcp_connect(tcp_socket, &ipaddr, port, nfs_connected);
    if (error != ERR_OK)
    {
        write_cyan("Error connecting\n");
        labelnum("Error: ", error);
        return 1;
    }
    return 0;
}

int nfs_socket_send(char *buf, int len)
{
    write_cyan("Sending to NFS server\n");
    // tcp_write(tcp_socket, buf, len, 1);
    return 0;
}
