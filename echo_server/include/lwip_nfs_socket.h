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

int create_socket(void);

int socket_connect(void);