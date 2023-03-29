#pragma once

#include "shared_ringbuffer.h"

typedef struct nfs_state
{
    /* Pointers to shared buffers */
    ring_handle_t rx_ring;
    ring_handle_t tx_ring;
    /*
     * Metadata associated with buffers
     */
} nfs_state_t;

int nfs_socket_create();
int nfs_socket_connect(int fd, int port);
int nfs_socket_close(int fd);
void nfs_init_sockets(void);