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
    if (p == NULL) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    uintptr_t data;
    unsigned int len;
    void *cookie;

    int error = dequeue_avail(&websrv_state.rx_ring, &data, &len, &cookie);
    if (error) {
        sel4cp_dbg_puts("Failed to dequeue avail from rx_ring\n");
        return ERR_OK;
    }

    pbuf_copy_partial(p, (void *)data, p->tot_len, 0);

    cookie = (void *)tpcb;

    enqueue_used(&websrv_state.rx_ring, data, p->tot_len, cookie);

    sel4cp_notify(WEBSRV_CH);
    return ERR_OK;
}

int websrv_socket_send_response() {

    uintptr_t tx_buf;
    unsigned int len;
    void *cookie;
    
    err_t error = (err_t)dequeue_used(&websrv_state.tx_ring, &tx_buf, &len, &cookie);
    if (error) {
        sel4cp_dbg_puts("Failed to dequeue used from tx_ring\n");
        return -1;
    }

    error = tcp_write((struct tcp_pcb *)cookie, (char *)tx_buf, len, 1);
    if (error) {
        sel4cp_dbg_puts("Failed to queue TCP packet to socket");
    }
    enqueue_avail(&websrv_state.tx_ring, tx_buf, BUF_SIZE, NULL);

    return (int)error;
}

static err_t websrv_socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    return ERR_OK;
}

static err_t websrv_socket_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_sent(newpcb, websrv_socket_sent_callback);
    tcp_recv(newpcb, websrv_socket_recv_callback);
    return ERR_OK;
}

int setup_tcp_socket(void) 
{
    tcp_socket = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (tcp_socket == NULL) {
        sel4cp_dbg_puts("Failed to open a socket for listening!");
        return -1;
    }

    err_t error = tcp_bind(tcp_socket, IP_ANY_TYPE, TCP_SERVER_PORT);
    if (error) {
        sel4cp_dbg_puts("Failed to bind the TCP socket");
        return -1;
    } else {
        sel4cp_dbg_puts("Utilisation port bound to port 1236");
    }

    tcp_socket = tcp_listen_with_backlog_and_err(tcp_socket, 1, &error);
    if (error != ERR_OK) {
        sel4cp_dbg_puts("Failed to listen on the utilization socket");
        return -1;
    }
    tcp_accept(tcp_socket, websrv_socket_accept_callback);

    return 0;
}