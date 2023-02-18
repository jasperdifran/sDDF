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

// Stores listening socket tcp_ptcb
static struct tcp_pcb *tcp_socket;
struct tcp_pcb *sockets[10];
int socket_count = 0;

int create_socket(void)
{
    int domain = sel4cp_mr_get(1);
    int type = sel4cp_mr_get(2);
    int protocol = sel4cp_mr_get(3);
    sel4cp_dbg_puts("Creating socket\n");
    labelnum("domain: ", domain);
    labelnum("type: ", type);
    labelnum("protocol: ", protocol);
    // if (domain != AF_INET || type != SOCK_STREAM || protocol != IPPROTO_TCP)
    // {
    //     return -1;
    // }
    // sockets[1] = tcp_new();
    // if (sockets[1] == NULL)
    // {
    //     return 0;
    // }
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
    // if (cmd == F_GETFL)
    // {
    //     return 0;
    // }
    return 0;
}

int bind_socket(void)
{
    int sockfd = sel4cp_mr_get(1);
    sel4cp_dbg_puts("Binding socket\n");
    // err_t error = tcp_bind(sockets[1], IP_ANY_TYPE, 2500);
    // if (error != ERR_OK)
    // {
    //     sel4cp_dbg_puts("Error binding socket\n");
    //     return 1;
    // }
    return 0;
}

// Connected function
err_t nfs_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    sel4cp_dbg_puts("Successfully connected!\n");
    return ERR_OK;
}

void err_func(void *arg, err_t err)
{
    sel4cp_dbg_puts("Error connecting!\n");
}

int socket_connect(void)
{
    return 0;
    ip_addr_t ipaddr;
    int port = 111;
    ip4_addr_set_u32(&ipaddr, ipaddr_addr("10.13.0.11"));
    tcp_socket = tcp_new_ip_type(IPADDR_TYPE_V4);
    tcp_err(tcp_socket, err_func);
    if (tcp_socket == NULL)
    {
        return 1;
    }
    err_t error = tcp_connect(tcp_socket, &ipaddr, port, nfs_connected);
    if (error != ERR_OK)
    {
        return 1;
    }
    else
    {
        sel4cp_dbg_puts("Connecting...\n");
    }
    return 0;
}