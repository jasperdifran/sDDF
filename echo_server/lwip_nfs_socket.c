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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

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

// #define NFS_SERVER_IP "10.13.1.90" // IP of Jasper's NFS server (when on keg)
#define NFS_SERVER_IP "10.13.0.11" // IP of NFSHomes

typedef struct
{
    struct tcp_pcb *sock_tpcb;
    int port;
    int fd;
    int connected;
    int used;
} nfs_socket_t;

static struct tcp_pcb *tcp_socket;

// Should only need 1 at any one time, accounts for any reconnecting that might happen
nfs_socket_t nfs_sockets[10] = {0};

extern nfs_state_t nfs_state;

int lwip_fcntl(void)
{
    return 0;
}

static err_t nfs_socket_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (p == NULL)
    {
        printf("Closing connection...\n");
        tcp_close(tpcb);
        return ERR_OK;
    }

    int len_to_read = p->tot_len;

    int len_read = 0;
    while (len_to_read > 0)
    {
        uintptr_t data;
        unsigned int discard_len;
        void *cookie;

        int error = dequeue_avail(&nfs_state.rx_ring, &data, &discard_len, &cookie);
        if (error)
        {
            printf("Failed to dequeue avail from rx_ring\n");
            return ERR_OK;
        }

        int len_to_read_this_round = MIN(len_to_read, BUF_SIZE);
        pbuf_copy_partial(p, (void *)data, len_to_read_this_round, len_read);

        cookie = (void *)tpcb;

        enqueue_used(&nfs_state.rx_ring, data, len_to_read_this_round, cookie);

        len_to_read -= len_to_read_this_round;
        len_read += len_to_read_this_round;
    }

    sel4cp_notify(LWIP_NFS_CH);
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t nfs_socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    return ERR_OK;
}

// Connected function
err_t nfs_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    ((nfs_socket_t *)arg)->connected = 1;
    tcp_sent(tpcb, nfs_socket_sent_callback);
    tcp_recv(tpcb, nfs_socket_recv_callback);
    sel4cp_notify(LWIP_NFS_CH);
    return ERR_OK;
}

void err_func(void *arg, err_t err)
{
    printf("Error %d with NFS socket %d", err, ((nfs_socket_t *)arg)->fd);
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
        printf("Error connecting\n");
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
int nfs_socket_close(int fd, int unused)
{
    (void)unused;
    nfs_socket_t *sock = &nfs_sockets[fd - NFS_SOCKET_FD_OFFSET];

    if (sock->used)
    {
        if (tcp_close(sock))
        {
            return -1;
            printf("Error closing socket\n");
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

    // We don't currently handle the case where there are no free sockets
    while (nfs_sockets[freeSocketInd].used)
        freeSocketInd++;
    nfs_socket_t *freeSocket = &nfs_sockets[freeSocketInd];

    freeSocket->sock_tpcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (freeSocket->sock_tpcb == NULL)
    {
        printf("Error creating socket\n");
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

int nfs_socket_dup3(int oldfd, int newfd)
{
    nfs_socket_t *old_sock = &nfs_sockets[oldfd - NFS_SOCKET_FD_OFFSET];

    if (newfd < NFS_SOCKET_FD_OFFSET || newfd >= NFS_SOCKET_FD_OFFSET + NFS_SOCKETS)
    {
        return EBADF;
    }
    nfs_socket_t *new_sock = &nfs_sockets[newfd - NFS_SOCKET_FD_OFFSET];

    tcp_close(new_sock->sock_tpcb);

    if (old_sock->used)
    {
        new_sock->sock_tpcb = old_sock->sock_tpcb;
        tcp_arg(new_sock->sock_tpcb, new_sock);
        new_sock->used = 1;
        new_sock->port = old_sock->port;
        return newfd;
    }
    return -1;
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
        nfs_sockets[i].port = -1;
        nfs_sockets[i].fd = i + NFS_SOCKET_FD_OFFSET;
    }
}