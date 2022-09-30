#include <sel4cp.h>

#include "lwip/ip.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "echo.h"

#define TCP_SERVER_PORT 80
#define WEBSRV_CH 3

static struct tcp_pcb *tcp_socket;

/**
 * @brief Echos for now. Must be adjusted to loop through linked list of struct pbuf.
 * 
 * @param arg 
 * @param tpcb 
 * @param p 
 * @param err 
 * @return err_t 
 */
static err_t lwip_tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    sel4cp_notify(WEBSRV_CH);
    err_t error = tcp_write(tpcb, p->payload, p->len, 1);
    if (error) {
        sel4cp_dbg_puts("Failed to send TCP packet through socket");
    }
}

int setup_tcp_socket() 
{
    tcp_socket = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (tcp_socket == NULL) {
        sel4cp_dbg_puts("Failed to open a TCP socket");
        return -1;
    }

    int error = tcp_bind(tcp_socket, IP_ANY_TYPE, TCP_SERVER_PORT);
    if (error == ERR_OK) {
        tcp_recv(tcp_socket, lwip_tcp_recv_callback);
    } else {
        sel4cp_dbg_puts("Failed to bind the TCP socket");
        return -1;
    }


}