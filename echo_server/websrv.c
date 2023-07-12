#include <sel4cp.h>
#include <sel4/sel4.h>
#include <websrvint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <syscall_implementation.h>

#include <memzip.h>

#include "shared_ringbuffer.h"
#include "echo.h"

#include "util.h"

#define LWIP_CH 6
#define NFS_CH 7

#define ETHER_MTU 1500
#define NUM_BUFFERS 512
#define BUF_SIZE 2048
#define MAX_REQUESTS 10

int req_ids = 0;

/**
 * @brief Struct to store data about a request. Used is a 1 or a 0 to indicate if the request is in use or not. id ranges
 * from 0 to MAX_REQUESTS-1. socket_id is the cookie received from lwip indicating the tcp socket.
 *
 */
typedef struct
{
    int id;
    int used;
    void *socket_id;
} request_data_t;

/**
 * @brief Array of request_data_t structs for storing data about one instance of a request. id is equal to the index into
 * this array
 *
 */
request_data_t requests[MAX_REQUESTS] = {0};

/**
 * @brief Array of buffers to store any data used by micropython which might be needed at a later date. It has
 * a limit of BUF_SIZE so is useful for `stat` responses or path names/method types.
 *
 */
char requests_private_data[MAX_REQUESTS][BUF_SIZE] = {0};

pid_t my_pid = WEBSRV_PID;

uintptr_t rx_websrv_avail;
uintptr_t rx_websrv_used;
uintptr_t tx_websrv_avail;
uintptr_t tx_websrv_used;

uintptr_t shared_websrv_lwip_vaddr;

uintptr_t rx_nfs_websrv_avail;
uintptr_t rx_nfs_websrv_used;
uintptr_t tx_nfs_websrv_avail;
uintptr_t tx_nfs_websrv_used;

uintptr_t shared_nfs_websrv_vaddr;

ring_handle_t lwip_rx_ring;
ring_handle_t lwip_tx_ring;
ring_handle_t nfs_rx_ring;
ring_handle_t nfs_tx_ring;

/**
 * @brief Buffer to store the data to be sent to the client. Only one request will write to this at any one time
 * and it will only write to it once the response is ready.
 *
 */
char tx_data[BUF_SIZE * 256] = {0};

/**
 * @brief Length of the data to be sent to the client. Only one request will write to this at any one time.
 *
 */
unsigned int tx_len;

/**
 * @brief Large buffer for storing any data received from NFS. Outgoing data can be passed as a parameter to
 * `stat_file` and `req_file`.
 *
 */
char nfs_received_data_store[BUF_SIZE * 10] = {0};

/**
 * @brief Length of the data received from NFS.
 *
 */
int nfs_received_data_store_len = 0;

/**
 * @brief Flag to indicate if this request has finished and a response is ready to be sent. Every time we run something
 * in micropython we expect the request to be done afterwards. This flag confirms so.
 *
 */
int request_done = 0;

/**
 * @brief The current request id. This is used to identify which request is currently being processed. This is also an
 * index into the requests array and the requests_private_data array.
 *
 */
int current_request_id = -1;

void init(void)
{
    syscalls_init();
    ring_init(&lwip_rx_ring, (ring_buffer_t *)rx_websrv_avail, (ring_buffer_t *)rx_websrv_used, NULL, 0);
    ring_init(&lwip_tx_ring, (ring_buffer_t *)tx_websrv_avail, (ring_buffer_t *)tx_websrv_used, NULL, 0);
    init_websrv();
    ring_init(&nfs_rx_ring, (ring_buffer_t *)rx_nfs_websrv_avail, (ring_buffer_t *)rx_nfs_websrv_used, NULL, 0);
    ring_init(&nfs_tx_ring, (ring_buffer_t *)tx_nfs_websrv_avail, (ring_buffer_t *)tx_nfs_websrv_used, NULL, 0);
    printf("Init websrv pd\n");

    for (int i = 0; i < MAX_REQUESTS; i++)
    {
        requests[i].id = i;
        requests[i].used = 0;
        requests[i].socket_id = NULL;
    }
}

void copy_mpybuf_to_ringbuf(void *cookie)
{
    /* Split response buf up into ring buf buffers */
    unsigned int bytes_written = 0;

    while (bytes_written < tx_len)
    {
        void *tx_cookie;
        uintptr_t tx_buf;
        unsigned int temp_len;

        if (ring_empty(lwip_tx_ring.avail_ring))
        {
            // lwip pd has a higher prio, if we notify it will for sure free up some buffers
            sel4cp_notify(LWIP_CH);
        }

        int error = dequeue_avail(&lwip_tx_ring, &tx_buf, &temp_len, &tx_cookie);
        if (error)
        {
            printf("Failed to dequeue avail from lwip_tx_ring\n");
            return;
        }

        unsigned int bytes_to_write = MIN((tx_len - bytes_written), BUF_SIZE);
        for (unsigned int i = 0; i < bytes_to_write; i++)
        {
            ((char *)tx_buf)[i] = tx_data[bytes_written + i];
        }
        bytes_written += bytes_to_write;

        enqueue_used(&lwip_tx_ring, tx_buf, bytes_to_write, cookie);
    }
}



/**
 * @brief Request the content of a file from the NFS server. Send in the following shape
 *
 * | 1 byte: SYS_OPENREADCLOSE | 4 bytes: int len_to_read | filename | '\0' |
 *
 * @param filename
 */
void req_file(const char *filename, int len_to_read)
{
    void *discard_cookie;
    uintptr_t tx_buf;
    unsigned int buf_len;

    int error = dequeue_avail(&nfs_tx_ring, &tx_buf, &buf_len, &discard_cookie);
    if (error) {
        printf("Failed to dequeue avail from nfs_tx_ring\n");
        return;
    }

    int pathLen = strlen(filename);
    char *buf = (char *)tx_buf;

    buf[0] = SYS_OPENREADCLOSE;
    split_int_to_buf(len_to_read, buf + 1);
    memcpy(buf + 5, filename, pathLen);
    buf[pathLen + 5] = '\0';

    error = enqueue_used(&nfs_tx_ring, tx_buf, MIN(pathLen + 6, BUF_SIZE), (void *)current_request_id);
    if (error) {
        printf("Failed to enqueue used to nfs_tx_ring\n");
        return;
    }

    sel4cp_notify(NFS_CH);
}

/**
 * @brief Request the stat of a file. Sends a buf to NFS server in the following shape:
 *
 * | 1 byte command: SYS_STAT64 | pathLen bytes; path | '\0' |
 *
 * @param filename
 */
void stat_file(const char *filename)
{
    void *discard_cookie;
    uintptr_t tx_buf;
    unsigned int buf_len;

    int err = dequeue_avail(&nfs_tx_ring, &tx_buf, &buf_len, &discard_cookie);
    if (err) {
        printf("Failed to dequeue from nfs_tx_ring\n");
        return;
    }
    int pathLen = strlen(filename);
    char *buf = (char *)tx_buf;
    buf[0] = SYS_STAT64;
    memcpy(buf + 1, filename, pathLen);
    buf[pathLen + 1] = '\0';
    err = enqueue_used(&nfs_tx_ring, tx_buf, pathLen + 2, (void *)current_request_id);
    if (err) {
        printf("Failed to enqueue used to nfs_tx_ring\n");
        return;
    }

    sel4cp_notify(NFS_CH);
}

/** Responses from NFS
    READ: [command (1 byte), file size (4 bytes), file data (n bytes)]
    Reads will be followed by however many buffers are required to hold the file
    STAT: [command (1 byte), file size (4 bytes), last mod date (2 bytes), last mod time (2 bytes), is_dir (1 byte)]
*/

/**
 * @brief Handle a read response from NFS.
 * 
 * In the case of a file being read, i.e. NFS is about to send it to lwip, we want to just send the headers required for
 * this reponse so rx_buf and len go unused, only continuation_id is used.
 * 
 * @param rx_buf 
 * @param len 
 * @param continuation_id 
 */
void handle_read_response(void *rx_buf, int len, int continuation_id)
{

    int len_to_read = get_int_from_buf((char *)rx_buf, 1);
    int len_read = 0;

    // We know we just got a command byte and a status byte
    // Copies until the end of this buffer
    int read_this_round = MIN(len_to_read, len - 5);
    memcpy(nfs_received_data_store, (void *)rx_buf + 5, read_this_round);
    len_read += read_this_round;
    len_to_read -= read_this_round;

    while (len_to_read > 0) {
        // Wait for the next buffer
        void *discard_cookie;
        uintptr_t temp_rx_buf;
        unsigned int buf_len;
        int error = dequeue_used(&nfs_rx_ring, &temp_rx_buf, &buf_len, &discard_cookie);
        if (error)
        {
            printf("Failed to dequeue avail from nfs_rx_ring\n");
            return;
        }
        int read_this_round = MIN(len_to_read, buf_len);
        memcpy(nfs_received_data_store + len_read, (void *)temp_rx_buf, read_this_round);
        len_read += read_this_round;
        len_to_read -= read_this_round;
        enqueue_avail(&nfs_rx_ring, temp_rx_buf, BUF_SIZE, NULL);
    }

    run_cont("readfilecont.py", 0, (void *)nfs_received_data_store, len_read, &requests_private_data[continuation_id], (char *)tx_data, &tx_len);
}

/**
 * @brief Handle a stat response from NFS. The shape of the incoming packet in the case of success:
 * 
 * | 1 byte: SYS_STAT64 | 4 byte int: file size | 2 byte int: last mod date | 2 byte int: last mod time | 1 byte: is_dir |
 * 
 * In the case of failure:
 * 
 * | 1 byte: SYS_STAT64 | 1 byte: error code. At this stage it is only ever 1 |
 * 
 * @param rx_buf Buffer already dequeued from the rx_ring
 * @param len 
 * @param continuation_id 
 */
void handle_stat_response(void *rx_buf, int len, int continuation_id)
{
    if (len != 2) {
        // File has been found, copy over the stat results
        memcpy(nfs_received_data_store, (void *)rx_buf + 1, len);
    }
    int status = run_cont("statcont.py", (len == 2), (void *)nfs_received_data_store, len, &requests_private_data[continuation_id], (char *)tx_data, &tx_len);
    
}

/**
 * @brief Handle an error response from NFS. The shape of the incoming packet:
 * | 1 byte: SYS_ERROR | 4 byte int: error code |
 * 
 * @param rx_buf 
 * @param len 
 * @param continuation_id 
 */
void handle_error_response(void *rx_buf, int len, int continuation_id)
{
    int error = get_int_from_buf((char *)rx_buf, 1);
    run_cont("errorcont.py", error, (void *)nfs_received_data_store, len, &requests_private_data[continuation_id], (char *)tx_data, &tx_len);
}

/**
 * @brief For all communications with NFS, our cookie will be the request id
 *
 */
void handle_nfs_response()
{
    void *local_current_request_id;
    uintptr_t rx_buf;
    unsigned int buf_len;

    if (dequeue_used(&nfs_rx_ring, &rx_buf, &buf_len, &local_current_request_id)) {
        printf("Failed to dequeue used from nfs_rx_ring\n");
        return;
    }
    char local_temp_buf[BUF_SIZE] = {0};
    memcpy(local_temp_buf, (void *)rx_buf, buf_len);
    enqueue_avail(&nfs_rx_ring, rx_buf, BUF_SIZE, NULL);

    current_request_id = (int)local_current_request_id;

    request_data_t *req = &requests[current_request_id];
    int operation_id = ((char *)local_temp_buf)[0];

    request_done = 0;
    switch (operation_id)
    {
    case SYS_ERROR:
        handle_error_response((char *)local_temp_buf, buf_len, (int)current_request_id);
        break;
    case SYS_STAT64:
        handle_stat_response((char *)local_temp_buf, buf_len, (int)current_request_id);
        break;
    case SYS_OPENREADCLOSE:
        handle_read_response((char *)local_temp_buf, buf_len, (int)current_request_id);
        break;
    default:
        printf("Unknown operation id: %d\n", operation_id);
        break;
    }

    if (request_done)
    {
        // If the request is done, send it off to lwip
        request_done = 0;

        copy_mpybuf_to_ringbuf(req->socket_id);
        sel4cp_notify(LWIP_CH);

        req->used = 0;
        req->socket_id = NULL;
    }
    current_request_id = -1;
}

/**
 * @brief Handle a new request from lwip. Upon entering this function we want to dequeue a buffer if available,
 * find a free continuation id and hence a free request_data_t struct, set the current_request_id and run the
 * webserver.
 *
 */
void handle_lwip_request()
{
    uintptr_t rx_buf;
    void *rx_cookie;
    unsigned int rx_len;

    int error = dequeue_used(&lwip_rx_ring, &rx_buf, &rx_len, &rx_cookie);
    if (error)
    {
        printf("websrv: Failed to dequeue used from lwip_rx_ring\n");
        return;
    }

    /* Init a response buf and process request */
    tx_len = 0;

    // Find a free continuation to use. For now, assuming there is one free
    // and we know that pretty much every request will be async.
    int contInd = 0;
    while (requests[contInd].used)
        contInd++;

    request_data_t *req = &requests[contInd];
    current_request_id = contInd;
    request_done = 0;

    req->used = 1;
    req->socket_id = rx_cookie;

    int status = run_webserver((char *)rx_buf, &requests_private_data[contInd], (char *)tx_data, &tx_len);
    if (status == -1)
    {
        printf("Error running webserver\n");
        return;
    }

    if (request_done)
    {
        request_done = 0;

        copy_mpybuf_to_ringbuf(rx_cookie);
        sel4cp_notify(LWIP_CH);

        req->used = 0;
        req->socket_id = NULL;
    }
    current_request_id = -1;

    /* Release req buf */
    enqueue_avail(&lwip_rx_ring, rx_buf, BUF_SIZE, NULL);
}

void notified(sel4cp_channel ch)
{
    switch (ch)
    {
    case LWIP_CH:;
        /* Incoming new request packet from lwip */
        while (!ring_empty(lwip_rx_ring.used_ring))
        {
            handle_lwip_request();
        }
        break;
    case NFS_CH:;
        /* Continuation of a request */
        handle_nfs_response();
        break;

    default:
        printf("Unknown notif\n");
        break;
    }
}
