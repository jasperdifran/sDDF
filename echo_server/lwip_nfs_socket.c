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
    sel4cp_dbg_puts("Recv callback\n");
    if (p == NULL)
    {
        sel4cp_dbg_puts("Closing connection...\n");
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

    print_bright_green_buf(data, p->tot_len);

    cookie = (void *)tpcb;

    enqueue_used(&nfs_state.rx_ring, data, p->tot_len, cookie);

    sel4cp_notify(LWIP_NFS_CH);
    tcp_recved(tpcb, p->tot_len);
    return ERR_OK;
}

static err_t nfs_socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    sel4cp_dbg_puts("Sent callback\n");
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
    labelnum("Error: ", err);
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
    write_cyan("Processing TX\n");
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

        labelnum("len: ", len);
        // print_bright_magenta_buf(data, len);

        error = tcp_write(tcp_socket, (void *)data, len, 1);
        if (error != ERR_OK)
        {
            sel4cp_dbg_puts("Failed to write to socket\n");
            return 1;
        }
        tcp_output(tcp_socket);
        enqueue_avail(&nfs_state.tx_ring, data, BUF_SIZE, cookie);
    }
    return 0;
}
