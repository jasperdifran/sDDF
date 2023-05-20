#include <sel4cp.h>
#include <sel4/sel4.h>
#include <websrvint.h>
#include <string.h>
#include <stdlib.h>

#include <syscall_implementation.h>

#include <memzip.h>

#include "shared_ringbuffer.h"
#include "echo.h"

#include "util.h"

#define MIN(a, b) (a < b) ? a : b
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? -(a) : (a))

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
    sel4cp_dbg_puts("Init websrv pd\n");

    for (int i = 0; i < MAX_REQUESTS; i++)
    {
        requests[i].id = i;
        requests[i].used = 0;
        requests[i].socket_id = NULL;
    }
}

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

void copy_mpybuf_to_ringbuf(void *cookie)
{
    /* Split response buf up into ring buf buffers */
    unsigned int bytes_written = 0;

    while (bytes_written < tx_len)
    {
        // sel4cp_dbg_puts("Copying mpybuf to ringbuf");
        // label_num("bytes_written: ", bytes_written);
        // label_num("tx_len: ", tx_len);
        void *tx_cookie;
        uintptr_t tx_buf;
        unsigned int temp_len;

        if (ring_empty(lwip_tx_ring.avail_ring))
        {
            sel4cp_dbg_puts("LWIP TX RING EMPTY\n");
            sel4cp_notify(LWIP_CH);
        }
        // while (ring_empty(&lwip_tx_ring));
        int error = dequeue_avail(&lwip_tx_ring, &tx_buf, &temp_len, &tx_cookie);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue avail from lwip_tx_ring\n");
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

void split_int_to_buf(int num, char *buf)
{
    buf[0] = (num >> 24) & 0xFF;
    buf[1] = (num >> 16) & 0xFF;
    buf[2] = (num >> 8) & 0xFF;
    buf[3] = num & 0xFF;
}

int get_int_from_buf(char *buf, int offset)
{
    int ret = 0;
    ret |= (buf)[offset + 0] << 24;
    ret |= (buf)[offset + 1] << 16;
    ret |= (buf)[offset + 2] << 8;
    ret |= (buf)[offset + 3] << 0;
    return ret;
}

/**
 * @brief Request the content of a file from the NFS server. Send in the following shape
 *
 * | SYS_OPENREADCLOSE | int len_to_read | filename | '\0' |
 *
 * @param filename
 */
void req_file(const char *filename, int len_to_read)
{
    // sel4cp_dbg_puts("Requesting file: ");
    // sel4cp_dbg_puts(filename);
    // sel4cp_dbg_puts("\n");
    // label_num("len_to_read: ", len_to_read);

    void *discard_cookie;
    uintptr_t tx_buf;
    unsigned int buf_len;

    int error = dequeue_avail(&nfs_tx_ring, &tx_buf, &buf_len, &discard_cookie);
    if (error) {
        sel4cp_dbg_puts("Failed to dequeue avail from nfs_tx_ring\n");
        return;
    }
    // label_num("tx_buf: ", tx_buf);
    int pathLen = strlen(filename);
    char *buf = (char *)tx_buf;
    buf[0] = SYS_OPENREADCLOSE;
    split_int_to_buf(len_to_read, buf + 1);
    memcpy(buf + 5, filename, pathLen);
    buf[pathLen + 5] = '\0';
    error = enqueue_used(&nfs_tx_ring, tx_buf, pathLen + 5, current_request_id);
    if (error) {
        sel4cp_dbg_puts("Failed to enqueue used to nfs_tx_ring\n");
        return;
    }

    sel4cp_notify(NFS_CH);
}

/**
 * @brief Request the stat of a file. Sends a buf to NFS server in the following shape:
 *
 * [[1 byte command: SYS_STAT64][pathLen bytes path][1 byte null terminator]]
 *
 * @param filename
 */
void stat_file(const char *filename)
{
    // sel4cp_dbg_puts("Requesting file stat: ");
    // sel4cp_dbg_puts(filename);
    // sel4cp_dbg_puts("\n");
    void *discard_cookie;
    uintptr_t tx_buf;
    unsigned int buf_len;

    int err = dequeue_avail(&nfs_tx_ring, &tx_buf, &buf_len, &discard_cookie);
    // label_num("tx_buf: ", tx_buf);
    // ring_buf_dbg(&nfs_tx_ring);
    if (err) {
        sel4cp_dbg_puts("Failed to dequeue from nfs_tx_ring\n");
        return;
    }
    int pathLen = strlen(filename);
    char *buf = (char *)tx_buf;
    buf[0] = SYS_STAT64;
    memcpy(buf + 1, filename, pathLen);
    buf[pathLen + 1] = '\0';
    err = enqueue_used(&nfs_tx_ring, tx_buf, pathLen + 1, current_request_id);
    if (err) {
        sel4cp_dbg_puts("Failed to enqueue used to nfs_tx_ring\n");
        return;
    }

    sel4cp_notify(NFS_CH);
}

/** Responses from NFS
    READ: [command (1 byte), file size (4 bytes), file data (n bytes)]
    Reads will be followed by howeer many buffers are required to hold the file
    STAT: [command (1 byte), file size (4 bytes), last mod date (2 bytes), last mod time (2 bytes), is_dir (1 byte)]
*/

void handle_read_response(void *rx_buf, int len, int continuation_id)
{
    // int numBufs = (len - 5) / BUF_SIZE;
    // label_num("numBufs: ", numBufs);
    // label_num("len: ", len);
    // Store the size of the file
    // sel4cp_dbg_puts("Got read response\n");
    // memcpy(nfs_received_data_store, (void *)rx_buf + 5, len - 5);

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
        uintptr_t our_rx_buf;
        unsigned int buf_len;
        int error = dequeue_used(&nfs_rx_ring, &our_rx_buf, &buf_len, &discard_cookie);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue avail from nfs_rx_ring\n");
            return;
        }
        int read_this_round = MIN(len_to_read, buf_len);
        memcpy(nfs_received_data_store + len_read, (void *)our_rx_buf, read_this_round);
        len_read += read_this_round;
        len_to_read -= read_this_round;
        enqueue_avail(&nfs_rx_ring, our_rx_buf, BUF_SIZE, NULL);
    }

    // sel4cp_dbg_puts("Got read response\n");

    // print_bright_magenta_buf(nfs_received_data_store, len_read);

    run_cont("readfilecont.py", 0, (void *)nfs_received_data_store, len_read, &requests_private_data[continuation_id], (char *)tx_data, &tx_len);
}

void handle_stat_response(void *rx_buf, int len, int continuation_id)
{
    sel4cp_dbg_puts("Got stat response\n");
    if (len == 2)
    {
        // We know we just got a command byte and a status byte
        sel4cp_dbg_puts("File not found\n");
        int status = run_cont("statcont.py", 1, (void *)nfs_received_data_store, len, &requests_private_data[continuation_id], (char *)tx_data, &tx_len);
    }
    else
    {
        memcpy(nfs_received_data_store, (void *)rx_buf + 1, len);

        // label_num("len: ", len);
        // for (int i = 0; i < len; i++)
        // {
        //     printnum(((char *)rx_buf)[i]);
        //     sel4cp_dbg_puts(" ");
        // }

        int status = run_cont("statcont.py", 0, (void *)nfs_received_data_store, len, &requests_private_data[continuation_id], (char *)tx_data, &tx_len);
    }
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
    for (int i = 0; i < sizeof(void *); i++)
    {
        char_to_hex((addr >> (i * 8)) & 0xff, hex + (i * 2));
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

/**
 * @brief For all communications with NFS, our cookie will be the request id
 *
 */
void handle_nfs_response()
{
    // sel4cp_dbg_puts("Handling nfs response\n");
    void *local_current_request_id;
    uintptr_t rx_buf;
    unsigned int buf_len;

    if (dequeue_used(&nfs_rx_ring, &rx_buf, &buf_len, &local_current_request_id)) {
        sel4cp_dbg_puts("Failed to dequeue used from nfs_rx_ring\n");
        return;
    }
    char local_temp_buf[BUF_SIZE] = {0};
    memcpy(local_temp_buf, rx_buf, buf_len);
    enqueue_avail(&nfs_rx_ring, rx_buf, BUF_SIZE, NULL);

    current_request_id = (int)local_current_request_id;
    // label_num("Handling nfs response for req id: ", current_request_id);

    request_data_t *req = &requests[current_request_id];
    int operation_id = ((char *)local_temp_buf)[0];

    request_done = 0;
    switch (operation_id)
    {
    case SYS_STAT64:
        handle_stat_response((char *)local_temp_buf, buf_len, (int)current_request_id);
        break;
    case SYS_OPENREADCLOSE:
        handle_read_response((char *)local_temp_buf, buf_len, (int)current_request_id);
        break;
    default:
        sel4cp_dbg_puts("Unknown operation id: ");
        printnum(operation_id);
        sel4cp_dbg_puts("\n");
        break;
    }

    if (request_done)
    {
        // sel4cp_dbg_puts("Continuation done\n");
        request_done = 0;
        // label_num("Continuation done, tx_len: ", tx_len);
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
        sel4cp_dbg_puts("websrv: Failed to dequeue used from lwip_rx_ring\n");
        return;
    }

    // sel4cp_dbg_puts("RX_COOKIE: ");
    // print_addr(rx_cookie);

    /* Init a response buf and process request */
    tx_len = 0;

    // Find a free continuation to use. For now, assuming there is one free
    // and we know that pretty much every request will be async.
    // sel4cp_dbg_puts("Looking for free continuation\n");
    int contInd = 0;
    while (requests[contInd].used)
        contInd++;
    // sel4cp_dbg_puts("Found free continuation\n");
    request_data_t *req = &requests[contInd];
    current_request_id = contInd;
    request_done = 0;
    // label_num("Handling run_webserver req id: ", current_request_id);
    req->used = 1;
    req->socket_id = rx_cookie;

    // sel4cp_dbg_puts("SOCKET ID: ");
    // print_addr(req->socket_id);
    // sel4cp_dbg_puts("SOCKET_ID ADDR: ");
    // print_addr(&req->socket_id);

    // sel4cp_dbg_puts("Running webserver\n");

    // We know that pretty much every request will be async
    // run_webserver((char *)rx_buf, (char *)tx_data, &tx_len);
    sel4cp_dbg_puts("Webserver about to run\n");
    int status = run_webserver((char *)rx_buf, &requests_private_data[contInd], (char *)tx_data, &tx_len);
    sel4cp_dbg_puts("Webserver returned\n");

    if (request_done)
    {
        // sel4cp_dbg_puts("Continuation done\n");
        request_done = 0;
        // label_num("Continuation done, tx_len: ", tx_len);
        sel4cp_dbg_puts((char *)tx_data);
        copy_mpybuf_to_ringbuf(rx_cookie);
        sel4cp_notify(LWIP_CH);

        req->used = 0;
        req->socket_id = NULL;
    }
    current_request_id = -1;

    /* Copy response buf to ring buf */

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
            // sel4cp_dbg_puts("Websrv got a request from lwip\n");
            handle_lwip_request();
        }
        break;
    case NFS_CH:;
        /* Continuation of a request */
        // sel4cp_dbg_puts("Websrv got a reponse from NFS CH\n");
        handle_nfs_response();
        break;

    default:
        sel4cp_dbg_puts("Unknown notif\n");
        break;
    }
}
