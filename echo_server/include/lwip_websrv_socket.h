#pragma once

#include "shared_ringbuffer.h"

typedef struct websrv_state {
    /* Pointers to shared buffers */
    ring_handle_t rx_ring;
    ring_handle_t tx_ring;
    /*
     * Metadata associated with buffers
     */
} websrv_state_t;

int websrv_setup_tcp_socket(void);