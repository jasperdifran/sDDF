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

#define BUF_SIZE 2048

#define NFS_SOCKETS 10
#define NFS_SOCKET_FD_OFFSET 100

#define NFS_SERVER_IP "10.13.1.90" // IP of Mac NFS server
// #define NFS_SERVER_IP "10.13.0.11" // IP of NFSHomes

typedef struct
{
    struct tcp_pcb *sock_tpcb;
    int port;
    int fd;
    int connected;
    int used;
} nfs_socket_t;

static struct tcp_pcb *tcp_socket;
nfs_socket_t nfs_sockets[10] = {0};

extern nfs_state_t nfs_state;

void write_cyan(const char *str)
{
    sel4cp_dbg_puts("\033[36m");
    sel4cp_dbg_puts(str);
    sel4cp_dbg_puts("\033[0m");
}

int fcntl(void)
{
    int fd = sel4cp_mr_get(1);
    int cmd = sel4cp_mr_get(2);
    int arg = sel4cp_mr_get(3);
    sel4cp_dbg_puts("fcntl\n");
    labelnum("fd", fd);
    labelnum("cmd", cmd);
    labelnum("arg", arg);
    return 0;
}

static err_t nfs_socket_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (p == NULL)
    {
        sel4cp_dbg_puts("Closing connection...\n");
        sel4cp_dbg_puts("Closing socket ");
        sel4cp_dbg_puts(ip4addr_ntoa(&tpcb->remote_ip));
        sel4cp_dbg_puts(":");
        labelnum("", tpcb->remote_port);
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

    sel4cp_notify(LWIP_NFS_CH);
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
    labelnum("Error with NFS socket", ((nfs_socket_t *)arg)->fd);
}

/**
 * @brief Connect to an address. Corresponds to connect() in POSIX.
 *
 * @param fd
 * @param port
 * @return int
 */
int nfs_socket_connect(int fd, int port)
{

    nfs_socket_t *sock = &nfs_sockets[fd - NFS_SOCKET_FD_OFFSET];
    sock->port = port;

    ip_addr_t ipaddr;
    ip4_addr_set_u32(&ipaddr, ipaddr_addr(NFS_SERVER_IP));

    err_t error = tcp_connect(sock->sock_tpcb, &ipaddr, port, nfs_connected);
    if (error != ERR_OK)
    {
        write_cyan("Error connecting\n");
        return 1;
    }
    return 0;
}

/**
 * @brief Close a socket. Corresponds to close() in POSIX.
 *
 * @param fd. File descriptor for the socket
 * @return int. Returns 0 on success, -1 on failure
 */
int nfs_socket_close(int fd)
{
    nfs_socket_t *sock = &nfs_sockets[fd - NFS_SOCKET_FD_OFFSET];

    sel4cp_dbg_puts("Closing socket ");
    sel4cp_dbg_puts(ip4addr_ntoa(&sock->sock_tpcb->remote_ip));
    labelnum("", sock->sock_tpcb->remote_port);

    if (sock->used)
    {
        if (tcp_close(sock))
        {
            return -1;
        }
    }
    sock->used = 0;
    sock->sock_tpcb = NULL;
    sock->port = -1;
    return 0;
}

/**
 * @brief Create a socket object. Corresponds to socket() in POSIX.
 *
 * @return integer. File descriptor for the new socket
 */
int nfs_socket_create(void)
{
    int freeSocketInd = 0;
    while (nfs_sockets[freeSocketInd].used)
        freeSocketInd++;
    nfs_socket_t *freeSocket = &nfs_sockets[freeSocketInd];

    freeSocket->sock_tpcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (freeSocket->sock_tpcb == NULL)
    {
        write_cyan("Error creating socket\n");
        return 1;
    }

    freeSocket->used = 1;

    tcp_err(freeSocket->sock_tpcb, err_func);
    tcp_arg(freeSocket->sock_tpcb, freeSocket);

    int i = 512;
    while (tcp_bind(freeSocket->sock_tpcb, IP_ADDR_ANY, i) != ERR_OK)
        i++;
    return freeSocket->fd;
}

void char_to_hex(char c, char *buf)
{
    char *hex = "0123456789ABCDEF";
    buf[0] = hex[(c >> 4) & 0xF];
    buf[1] = hex[c & 0xF];
}

void print_addr(void *ptr)
{
    uintptr_t addr = (uintptr_t)ptr;
    char hex[16];
    for (int i = 0; i < 16; i++)
    {
        char_to_hex(((char *)&addr)[i], &hex[i * 2]);
    }
    sel4cp_dbg_puts(hex);
    sel4cp_dbg_puts("\n");
}

void print_buf(uintptr_t buf)
{
    print_buf_len(buf, 2048);
}

void print_buf_len(uintptr_t buf, int len)
{
    int zeroes = 0;
    for (int i = 0; i < len; i++)
    {
        char c = ((char *)buf)[i];
        char hex[2];
        // if (c == 0)
        // {
        //     zeroes++;
        //     if (zeroes > 10)
        //     {
        //         break;
        //     }
        // }
        // else
        // {
        //     zeroes = 0;
        // }
        char_to_hex(c, hex);
        sel4cp_dbg_putc(hex[0]);
        sel4cp_dbg_putc(hex[1]);
        sel4cp_dbg_putc(' ');
    }
}

void print_bright_magenta_buf(uintptr_t buf, int len)
{
    sel4cp_dbg_puts("\033[1;35m");
    print_buf_len(buf, len);
    sel4cp_dbg_puts("\033[0m\n");
}

void print_bright_green_buf(uintptr_t buf, int len)
{
    sel4cp_dbg_puts("\033[1;32m");
    print_buf_len(buf, len);
    sel4cp_dbg_puts("\033[0m\n");
}

int nfs_socket_process_tx(void)
{
    err_t error = ERR_OK;
    while (!ring_empty(nfs_state.tx_ring.used_ring))
    {
        uintptr_t data;
        unsigned int len;
        void *cookie;

        error = (err_t)dequeue_used(&nfs_state.tx_ring, &data, &len, &cookie);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue used from tx_ring\n");
            return 1;
        }

        int fd = (int)cookie;
        nfs_socket_t *sock = &nfs_sockets[fd - NFS_SOCKET_FD_OFFSET];

        error = tcp_write(sock->sock_tpcb, (void *)data, len, 1);
        if (error != ERR_OK)
        {
            sel4cp_dbg_puts("Failed to write to socket\n");
            return 1;
        }
        tcp_output(sock->sock_tpcb);
        enqueue_avail(&nfs_state.tx_ring, data, BUF_SIZE, 0);
    }
    return 0;
}

void nfs_init_sockets(void)
{
    for (int i = 0; i < NFS_SOCKETS; i++)
    {
        nfs_sockets[i].sock_tpcb = NULL;
        nfs_sockets[i].port = -1;
        nfs_sockets[i].fd = i + NFS_SOCKET_FD_OFFSET;
        nfs_sockets[i].connected = 0;
        nfs_sockets[i].used = 0;
    }
}