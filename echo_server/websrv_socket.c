#include <sel4cp.h>

#include "lwip/ip.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "echo.h"

static struct tcp_pcb *tcp_socket;

uintptr_t inc_packet;

#define WHOAMI "100 WEBSRV V1.0\n"

char aadata_buf[1024] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 15\r\n\r\nGosh, a request";

struct tcp_pcb *temp_websrv_socket = NULL;
char somebuf[1024];

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
    sel4cp_dbg_puts("Received bytes from TCP socket\n");
    if (p == NULL) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    pbuf_copy_partial(p, somebuf, p->tot_len, 0);
    sel4cp_dbg_puts(somebuf);


    sel4cp_dbg_puts("Received bytes in TCP echo\n");
    temp_websrv_socket = tpcb;
    sel4cp_notify(WEBSRV_CH);
    // err_t error = tcp_write(tpcb, aadata_buf, strlen(aadata_buf), 1);
    // if (error) {
    //     sel4cp_dbg_puts("Failed to send TCP packet through socket");
    // }
}

int websrv_socket_send_response() {
    sel4cp_dbg_puts("websrv_socket: Sending response\n");
    err_t error = tcp_write(temp_websrv_socket, aadata_buf, strlen(aadata_buf), 1);
    if (error) {
        sel4cp_dbg_puts("Failed to queue TCP packet to socket");
    }
    error = tcp_output(temp_websrv_socket);
    if (error) {
        sel4cp_dbg_puts("Failed to send TCP packet through socket");
    }
    sel4cp_dbg_puts("Sent response\n");
    tcp_close(temp_websrv_socket);
    temp_websrv_socket = NULL;
    return (int)error;
}



static err_t websrv_socket_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    return ERR_OK;
}

static err_t websrv_socket_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    sel4cp_dbg_puts("Utilization connection established!\n");
    // err_t error = tcp_write(newpcb, WHOAMI, strlen(WHOAMI), TCP_WRITE_FLAG_COPY);
    // sel4cp_dbg_puts("Sent WHOAMI message through utilization peer");
    // if (error) {
    //     sel4cp_dbg_puts("Failed to send WHOAMI message through utilization peer");
    // }
    tcp_sent(newpcb, websrv_socket_sent_callback);
    tcp_recv(newpcb, websrv_socket_recv_callback);
    sel4cp_dbg_puts("Attached callbacks to socket\n");
    return ERR_OK;
}

int setup_tcp_socket(void) 
{
    // tcp_socket = tcp_new_ip_type(IPADDR_TYPE_V4);
    // if (tcp_socket == NULL) {
    //     sel4cp_dbg_puts("Failed to open a TCP socket");
    //     return -1;
    // }

    // int error = tcp_bind(tcp_socket, IP_ANY_TYPE, TCP_SERVER_PORT);
    // if (error == ERR_OK) {
    //     tcp_recv(tcp_socket, lwip_tcp_recv_callback);
    // } else {
    //     sel4cp_dbg_puts("Failed to bind the TCP socket");
    //     return -1;
    // }
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