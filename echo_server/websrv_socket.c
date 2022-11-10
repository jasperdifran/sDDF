/**
 * @file websrv_socket.c
 * @author Jasper Di Francesco
 * @brief This file exposes the socket interface for the webserver. It exists in the LWIP PD.
 * @version 0.1
 * @date 2022-10-06
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <sel4cp.h>
#include <string.h>

#include "lwip/ip.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "echo.h"
#include "shared_ringbuffer.h"
#include "websrv_socket.h"
#include "util.h"

#define BUF_SIZE 2048

static struct tcp_pcb *tcp_socket;

extern websrv_state_t websrv_state;

#define WHOAMI "100 WEBSRV V1.0\n"

void printnum(int num)
{
    char buf[10];
    int i = 0;
    if (num == 0)
    {
        sel4cp_dbg_putc('0');
        return;
    }
    while (num > 0)
    {
        buf[i] = num % 10 + '0';
        num /= 10;
        i++;
    }
    // reverse buf
    for (int j = 0; j < i / 2; j++)
    {
        char tmp = buf[j];
        buf[j] = buf[i - j - 1];
        buf[i - j - 1] = tmp;
    }
    buf[i] = '\0';
    sel4cp_dbg_puts(buf);
}
void label_num(char *s, int n)
{
    sel4cp_dbg_puts(s);
    printnum(n);
    sel4cp_dbg_puts("\n");
}

/**
 * @brief Echos for now. Must be adjusted to loop through linked list of struct pbuf.
 *
 * @param arg
 * @param tpcb
 * @param p
 * @param err
 * @return err_t
 */
static err_t websrv_socket_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (p == NULL)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    uintptr_t data;
    unsigned int len;
    void *cookie;

    int error = dequeue_avail(&websrv_state.rx_ring, &data, &len, &cookie);
    if (error)
    {
        sel4cp_dbg_puts("Failed to dequeue avail from rx_ring\n");
        return ERR_OK;
    }

    pbuf_copy_partial(p, (void *)data, p->tot_len, 0);

    cookie = (void *)tpcb;

    enqueue_used(&websrv_state.rx_ring, data, p->tot_len, cookie);

    sel4cp_notify(WEBSRV_CH);
    tcp_recved(tpcb, p->tot_len);
    return ERR_OK;
}

uintptr_t oom_buf;
unsigned int oom_len;
struct tcp_pcb *oom_pcb;
int failed_write = 0;

err_t websrv_oom_retry_write()
{
    // sel4cp_dbg_puts("websrv_oom_retry_write... ");
    int ret = tcp_write(oom_pcb, (char *)oom_buf, oom_len, 1);
    if (ret != ERR_OK)
    {
        // sel4cp_dbg_puts("Failed\n");
        tcp_output(oom_pcb);
        return ERR_OK;
    }
    // sel4cp_dbg_puts("Success!\n");

    enqueue_avail(&websrv_state.tx_ring, oom_buf, BUF_SIZE, NULL);
    oom_buf = 0;
    oom_len = 0;
    oom_pcb = NULL;
    failed_write = 0;

    websrv_socket_send_response();

    return ERR_OK;
}

int websrv_socket_send_response()
{
    if (failed_write)
    {
        websrv_oom_retry_write();
        return -1;
    }
    err_t error = ERR_OK;
    while (!ring_empty(websrv_state.tx_ring.used_ring))
    {
        // sel4cp_dbg_puts("Attempting to send response\n");
        uintptr_t tx_buf;
        unsigned int len;
        void *cookie;
        error = (err_t)dequeue_used(&websrv_state.tx_ring, &tx_buf, &len, &cookie);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue used from tx_ring\n");
            return -1;
        }

        error = tcp_write((struct tcp_pcb *)cookie, (char *)tx_buf, len, 1);
        // Note this is going to happen as soon as files are over about 10k
        if (error == ERR_MEM)
        {
            // sel4cp_dbg_puts("Out of memory\n");
            oom_buf = tx_buf;
            oom_len = len;
            oom_pcb = (struct tcp_pcb *)cookie;
            failed_write = 1;
            tcp_output((struct tcp_pcb *)cookie);
            return -1;
        }
        tcp_output((struct tcp_pcb *)cookie);
        enqueue_avail(&websrv_state.tx_ring, tx_buf, BUF_SIZE, NULL);
    }

    return (int)error;
}

static err_t websrv_socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    // sel4cp_dbg_puts("websrv_socket_sent_callback\n");
    // label_num("Tx used ring empty: ", ring_empty(websrv_state.tx_ring.used_ring));
    // label_num("Tx used ring empty: ", ring_empty(websrv_state.tx_ring.avail_ring));
    if (failed_write)
    {
        // sel4cp_dbg_puts("Failed write, retrying\n");
        websrv_oom_retry_write();
    }
    return ERR_OK;
}

static err_t websrv_socket_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    sel4cp_dbg_puts("websrv_socket_accept new connection\n");
    label_num("Websrv socket addr: ", (uintptr_t)newpcb);
    if (newpcb == NULL)
    {
        sel4cp_dbg_puts("newpcb == NULL\n");
        label_num("Err num: ", err);
        // return ERR_OK;
    }
    tcp_sent(newpcb, websrv_socket_sent_callback);
    tcp_recv(newpcb, websrv_socket_recv_callback);
    return ERR_OK;
}

int setup_tcp_socket(void)
{
    tcp_socket = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (tcp_socket == NULL)
    {
        sel4cp_dbg_puts("Failed to open a socket for listening!");
        return -1;
    }
    else
    {
        sel4cp_dbg_puts("Opened a socket for listening!");
    }

    err_t error = tcp_bind(tcp_socket, IP_ANY_TYPE, TCP_SERVER_PORT);
    if (error)
    {
        sel4cp_dbg_puts("Failed to bind the TCP socket");
        return -1;
    }
    else
    {
        sel4cp_dbg_puts("Utilisation port bound to port 80");
    }

    tcp_socket = tcp_listen_with_backlog_and_err(tcp_socket, 5, &error);
    if (error != ERR_OK)
    {
        sel4cp_dbg_puts("Failed to listen on the TCP socket");
        return -1;
    }
    tcp_accept(tcp_socket, websrv_socket_accept_callback);

    return 0;
}