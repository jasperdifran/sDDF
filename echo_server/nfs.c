#include <sel4cp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>
#include <sys/types.h>
#include <syscall_implementation.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "shared_ringbuffer.h"

#include "util.h"

#define WEBSRV_CH 7
#define LWIP_NFS_CH 8
#define TIMER_CH 10

#define MY_PID NFS_PID

#define NUM_BUFFERS 512
#define BUF_SIZE 2048
#define NUM_OPEN_FILES 40

#define SERVER "10.13.0.11"
#define EXPORT "/export/home/imx8mm"
// #define EXPORT "/System/Volumes/Data/Users/jasperdifrancesco/export/imx8mm"
// #define EXPORT "/Users/jasperdifrancesco/export"

typedef struct
{
    struct nfsfh *nfsfh;
    int continuation_id;
    int file_descriptor;
    int used;
} nfs_open_file_t;

/**
 * @brief Micropython stat structure. Mpy expects DOS time, hence the date and time split
 *
 */
typedef struct
{
    uint32_t file_size;
    uint16_t last_mod_date;
    uint16_t last_mod_time;
    uint8_t is_dir;
} mpy_stat_t;

typedef struct
{
    int idx;
    int request_id;
    int len_to_read;
    int used;
    struct nfsfh *file_handle;
} nfs_openreadclose_data_t;

pid_t my_pid = NFS_PID;
extern socket_send_t nfs_send_to_lwip;
extern socket_recv_t nfs_recv_from_lwip;
extern socket_close_t nfs_close_lwip_sock;

nfs_openreadclose_data_t **nfs_openreadclose_data = NULL;
int max_open_files = 16;

struct pollfd pfds[2]; /* nfs:0  mount:1 */
struct nfs_context *nfs;

void labelnum(char *s, uint64_t a);

uintptr_t rx_nfs_avail;
uintptr_t rx_nfs_used;
uintptr_t tx_nfs_avail;
uintptr_t tx_nfs_used;

uintptr_t shared_nfs_lwip_vaddr;

uintptr_t rx_nfs_websrv_avail;
uintptr_t rx_nfs_websrv_used;
uintptr_t tx_nfs_websrv_avail;
uintptr_t tx_nfs_websrv_used;

uintptr_t shared_nfs_websrv_vaddr;

ring_handle_t lwip_rx_ring;
ring_handle_t lwip_tx_ring;
ring_handle_t websrv_rx_ring;
ring_handle_t websrv_tx_ring;

bool nfs_init = false;
bool nfs_socket_connected = false;

void nfs_connect_cb(int err, struct nfs_context *nfs_ctx, void *data, void *private_data)
{
    if (err != 0)
    {
        sel4cp_dbg_puts("nfs_connect_cb: failed to connect to nfs server\n");
        sel4cp_dbg_puts(nfs_get_error(nfs));
        sel4cp_dbg_puts("\n");
    }
    else
    {
        sel4cp_dbg_puts("nfs_connect_cb: connected to nfs server\n");
    }
}

void init_post(void)
{
    sel4cp_dbg_puts("init_post: starting nfs client\n");

    nfs = nfs_init_context();

    if (nfs == NULL)
    {
        sel4cp_dbg_puts("init: failed to init nfs context\n");
        return;
    }

    if (nfs_mount_async(nfs, SERVER, EXPORT, nfs_connect_cb, NULL) != 0)
    {
        sel4cp_dbg_puts("init: failed to connect to nfs server\n");
        return;
    }
    sel4cp_dbg_puts("init_post: connecting to nfs server\n");
}

/**
 * @brief UTIL function to split an int into four chars
 *
 * @param num
 * @param buf
 */
void split_int_to_buf(int num, char *buf)
{
    buf[0] = (num >> 24) & 0xFF;
    buf[1] = (num >> 16) & 0xFF;
    buf[2] = (num >> 8) & 0xFF;
    buf[3] = num & 0xFF;
}

static void __nfs_send_to_lwip(int fd, void *buffer, size_t len)
{
    char *buf = (char *)buffer;
    unsigned int bytes_written = 0;

    while (bytes_written < len)
    {
        void *tx_cookie;
        uintptr_t tx_buf;
        unsigned int temp_len;

        if (ring_empty(lwip_tx_ring.avail_ring))
        {
            sel4cp_notify(LWIP_NFS_CH);
        }

        int error = dequeue_avail(&lwip_tx_ring, &tx_buf, &temp_len, &tx_cookie);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue avail from lwip_tx_ring\n");
            return;
        }

        unsigned int bytes_to_write = MIN((len - bytes_written), BUF_SIZE);
        for (unsigned int i = 0; i < bytes_to_write; i++)
        {
            ((char *)tx_buf)[i] = buf[bytes_written + i];
        }
        bytes_written += bytes_to_write;

        error = enqueue_used(&lwip_tx_ring, tx_buf, bytes_to_write, (uintptr_t)fd);
        if (error)
        {
            sel4cp_dbg_puts("Failed to enqueue used to lwip_tx_ring\n");
            return;
        }
    }
    sel4cp_notify(LWIP_NFS_CH);
}

int nfs_socket_read = 0, nfs_socket_write = 0;
char *nfs_socket_buf[BUF_SIZE];

static size_t __nfs_recv_from_lwip(int fd, void *buffer, size_t len)
{
    char *buf = (char *)buffer;
    unsigned int bytes_read = 0;

    // Check if we can first read leftover data
    while (nfs_socket_read != nfs_socket_write && bytes_read < len)
    {
        buf[bytes_read] = nfs_socket_buf[nfs_socket_read];
        nfs_socket_read++;
        nfs_socket_read %= BUF_SIZE;
        bytes_read++;
    }

    int errcount = 0;

    while (bytes_read < len)
    {
        void *rx_cookie;
        uintptr_t rx_buf;
        unsigned int temp_len;

        int error = dequeue_used(&lwip_rx_ring, &rx_buf, &temp_len, &rx_cookie);
        if (error)
        {
            sel4cp_notify(LWIP_NFS_CH);
            errcount++;
            // Check that nothing is available from lwip 3 times before giving up
            if (errcount > 3)
                return bytes_read;
            else
                continue;
        }
        else
        {
            errcount = 0;
        }

        // Copy from rx_buf to buf
        unsigned int bytes_to_read = MIN((len - bytes_read), temp_len);
        for (unsigned int i = 0; i < bytes_to_read; i++)
        {
            buf[bytes_read + i] = ((char *)rx_buf)[i];
        }
        bytes_read += bytes_to_read;

        // More in the buffer than NFS is requesting. Fill our socket_buf with what's left. Will be
        // at most BUF_SIZE - 1 bytes
        if (temp_len > bytes_to_read)
        {
            for (unsigned int i = 0; i < temp_len - bytes_to_read; i++)
            {
                nfs_socket_buf[nfs_socket_write] = ((char *)rx_buf)[bytes_to_read + i];
                nfs_socket_write++;
                nfs_socket_write %= BUF_SIZE;
            }
        }

        error = enqueue_avail(&lwip_rx_ring, rx_buf, BUF_SIZE, 0);
        if (error)
        {
            sel4cp_dbg_puts("Failed to enqueue avail to lwip_rx_ring\n");
            return 0;
        }
    }
    return bytes_read;
}

/**
 * @brief Flushes the open lwip socket
 *
 * @return size_t
 */
static size_t __nfs_close_lwip_sock()
{
    nfs_socket_connected = false;
    // Empty the socket of incoming data
    while (!ring_empty(lwip_rx_ring.used_ring))
    {
        void *rx_cookie;
        uintptr_t rx_buf;
        unsigned int temp_len;

        int error = dequeue_used(&lwip_rx_ring, &rx_buf, &temp_len, &rx_cookie);
        if (error)
        {
            sel4cp_dbg_puts("nfs: Close sock failed to dequeue used from lwip_rx_ring\n");
            return 0;
        }

        enqueue_avail(&lwip_rx_ring, rx_buf, BUF_SIZE, 0);
    }
    return 0;
}

void send_websrv_error(int request_id, int error)
{
    void *cookie_continuation_id;
    uintptr_t rx_buf;
    unsigned int buf_len;

    if (dequeue_avail(&websrv_rx_ring, &rx_buf, &buf_len, &cookie_continuation_id))
    {
        sel4cp_dbg_puts("Failed to dequeue avail from websrv_tx_ring\n");
        return;
    }

    ((char *)rx_buf)[0] = SYS_ERROR;
    split_int_to_buf(error, (char *)rx_buf + 1);

    if (enqueue_used(&websrv_rx_ring, rx_buf, sizeof(int) + 1, (uintptr_t)request_id))
    {
        sel4cp_dbg_puts("Failed to enqueue used to websrv_tx_ring\n");
        return;
    }
    sel4cp_notify(WEBSRV_CH);
}

int poll_lwip_socket(void)
{
    if (!nfs_socket_connected)
        return 0;

    int ret = 0;
    int events = nfs_which_events(nfs);
    if ((events & POLLOUT) && !ring_empty(lwip_tx_ring.avail_ring))
    {
        ret |= POLLOUT;
    }
    if ((events & POLLIN) && (!ring_empty(lwip_rx_ring.used_ring) || nfs_socket_read != nfs_socket_write))
    {
        ret |= POLLIN;
    }
    return ret;
}

static void mtime_to_dos_date_time(uint32_t seconds, uint16_t *date, uint16_t *time)
{
    struct tm *timeinfo = gmtime(&seconds);
    uint16_t dos_date = 0, dos_time = 0;
    dos_date = (timeinfo->tm_year - 80) << 9;
    dos_date |= (timeinfo->tm_mon + 1) << 5;
    dos_date |= timeinfo->tm_mday;
    dos_time = timeinfo->tm_hour << 11;
    dos_time |= timeinfo->tm_min << 5;
    dos_time |= timeinfo->tm_sec / 2;
    *date = dos_date;
    *time = dos_time;
}

static void nfs_stat64_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    int continuation_id = (int)private_data;
    if (status != 0)
    {
        // File not found
        sel4cp_dbg_puts("nfs_stat_async_cb: failed to stat file\n");
        sel4cp_dbg_puts(nfs_get_error(nfs));
        sel4cp_dbg_puts("\n");

        void *cookie_continuation_id;
        uintptr_t rx_buf;
        unsigned int buf_len;

        int error = dequeue_avail(&websrv_rx_ring, &rx_buf, &buf_len, &cookie_continuation_id);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue avail from websrv_tx_ring\n");
            return;
        }

        ((char *)rx_buf)[0] = SYS_STAT64;
        ((char *)rx_buf)[1] = 1;
        error = enqueue_used(&websrv_rx_ring, rx_buf, 2, (void *)continuation_id);
        if (error)
        {
            sel4cp_dbg_puts("Failed to enqueue used to websrv_rx_ring\n");
            return;
        }
        sel4cp_notify(WEBSRV_CH);
    }
    else
    {
        struct nfs_stat_64 *st = (struct nfs_stat_64 *)data;

        mpy_stat_t mpy_stat;

        mpy_stat.file_size = (uint32_t)st->nfs_size;
        mtime_to_dos_date_time(st->nfs_mtime, &mpy_stat.last_mod_date, &mpy_stat.last_mod_time);
        mpy_stat.is_dir = S_ISDIR(st->nfs_mode);

        void *cookie_continuation_id;
        uintptr_t rx_buf;
        unsigned int buf_len;

        int error = dequeue_avail(&websrv_rx_ring, &rx_buf, &buf_len, &cookie_continuation_id);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue avail from websrv_tx_ring\n");
            return;
        }

        ((char *)rx_buf)[0] = SYS_STAT64;
        memcpy((char *)rx_buf + 1, &mpy_stat, sizeof(mpy_stat_t));
        error = enqueue_used(&websrv_rx_ring, rx_buf, sizeof(mpy_stat_t), (void *)continuation_id);
        if (error)
        {
            sel4cp_dbg_puts("Failed to enqueue used to websrv_rx_ring\n");
            return;
        }
        sel4cp_notify(WEBSRV_CH);
    }
}

void nfs_close_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    if (status != 0)
    {
        sel4cp_dbg_puts("nfs_close_async_cb: failed to close file\n");
        sel4cp_dbg_puts(nfs_get_error(nfs));
        sel4cp_dbg_puts("\n");
        // TODO return 500, or fail and free everything? At this point all required data
        // for the request has been send to websrv)
    }
    else
    {
        // Close file once done
        nfs_openreadclose_data_t *data = ((nfs_openreadclose_data_t *)private_data);
        data->file_handle = 0;
        data->len_to_read = 0;
        data->request_id = 0;
        data->used = 0;
    }
}
void nfs_read_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    if (status != ((nfs_openreadclose_data_t *)private_data)->len_to_read)
    {
        sel4cp_dbg_puts("nfs_read_async_cb: failed to read file\n");
        sel4cp_dbg_puts(nfs_get_error(nfs));
        sel4cp_dbg_puts("\n");
        sel4cp_dbg_puts(data);
        sel4cp_dbg_puts("\n");
        send_websrv_error(((nfs_openreadclose_data_t *)private_data)->request_id, 500);
    }
    else
    {
        // Now we send the data back to the webserver
        // We need to send back the command ID and how many bytes we read first, they should be stuck onto the
        // front of the first buffer.
        void *cookie_continuation_id;
        uintptr_t rx_buf;
        unsigned int buf_len;

        int len_to_be_sent = status;
        int len_sent = 0;

        int error = dequeue_avail(&websrv_rx_ring, &rx_buf, &buf_len, &cookie_continuation_id);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue avail from websrv_tx_ring\n");
            send_websrv_error(((nfs_openreadclose_data_t *)private_data)->request_id, 500);
            return;
        }

        // Set first bit to id of op
        ((char *)rx_buf)[0] = SYS_OPENREADCLOSE;

        // Set next 4 to length of entire data
        split_int_to_buf(((nfs_openreadclose_data_t *)private_data)->len_to_read, (char *)rx_buf + 1);

        // Send the first buffer with the command ID and how many bytes we read. Can fit at most BUF_SIZE - len_sent
        // bytes in this buffer
        int send_this_round = MIN(len_to_be_sent, BUF_SIZE - 5);
        memcpy((char *)rx_buf + 5, (char *)data, send_this_round);
        len_sent += send_this_round;
        len_to_be_sent -= send_this_round;

        error = enqueue_used(&websrv_rx_ring, rx_buf, send_this_round + 5, (void *)((nfs_openreadclose_data_t *)private_data)->request_id);
        if (error)
        {
            sel4cp_dbg_puts("Failed to enqueue used to websrv_rx_ring\n");
            send_websrv_error(((nfs_openreadclose_data_t *)private_data)->request_id, 500);
            return;
        }

        while (len_to_be_sent > 0)
        {
            error = dequeue_avail(&websrv_rx_ring, &rx_buf, &buf_len, &cookie_continuation_id);
            if (error)
            {
                sel4cp_dbg_puts("Failed to dequeue avail from websrv_tx_ring\n");
                send_websrv_error(((nfs_openreadclose_data_t *)private_data)->request_id, 500);
                return;
            }

            send_this_round = MIN(len_to_be_sent, BUF_SIZE);

            memcpy((char *)rx_buf, (char *)data + len_sent, send_this_round);

            error = enqueue_used(&websrv_rx_ring, rx_buf, send_this_round, (void *)((nfs_openreadclose_data_t *)private_data)->request_id);
            if (error)
            {
                sel4cp_dbg_puts("Failed to enqueue used to websrv_tx_ring\n");
                send_websrv_error(((nfs_openreadclose_data_t *)private_data)->request_id, 500);
                return;
            }
            len_to_be_sent -= send_this_round;
            len_sent += send_this_round;
        }
        sel4cp_notify(WEBSRV_CH);
        nfs_close_async(nfs, ((nfs_openreadclose_data_t *)private_data)->file_handle, nfs_close_async_cb, private_data);
    }
}

void nfs_open_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    nfs_openreadclose_data_t *request_data = (nfs_openreadclose_data_t *)private_data;
    if (status != 0)
    {
        sel4cp_dbg_puts("nfs_open_async_cb: failed to open file\n");
        sel4cp_dbg_puts(nfs_get_error(nfs));
        sel4cp_dbg_puts("\n");
        send_websrv_error(request_data->request_id, 404);
    }
    else
    {
        request_data->file_handle = (struct nfsfh *)data;
        nfs_read_async(nfs, (struct nfsfh *)data, request_data->len_to_read, nfs_read_async_cb, private_data);
    }
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

void handle_openreadclose(void *request_id, uintptr_t rx_buf, unsigned int buf_len)
{
    int i = 0;
    while (i < max_open_files && nfs_openreadclose_data[i] != NULL && nfs_openreadclose_data[i]->used)
    {
        i++;
    }

    /* Note we don't free pointers to nfs_openreadclose_data_t structs as we can reuse them */
    if (i == max_open_files)
    {
        max_open_files *= 2;
        void *new = realloc(nfs_openreadclose_data, sizeof(nfs_openreadclose_data_t *) * max_open_files);
        if (new == NULL)
        {
            send_websrv_error(request_id, 500);
            return;
        }
        else
        {
            nfs_openreadclose_data = new;
        }
        nfs_openreadclose_data[i] = malloc(sizeof(nfs_openreadclose_data_t));
    }
    else if (nfs_openreadclose_data[i] == NULL)
    {
        nfs_openreadclose_data[i] = malloc(sizeof(nfs_openreadclose_data_t));
    }

    nfs_openreadclose_data[i]->used = 1;
    nfs_openreadclose_data[i]->request_id = request_id;
    nfs_openreadclose_data[i]->len_to_read = get_int_from_buf((char *)rx_buf, 1);

    nfs_open_async(nfs, (char *)rx_buf + 5, O_RDONLY, nfs_open_async_cb, (void *)nfs_openreadclose_data[i]);
}

void handle_webserver_request(void)
{
    // TODO: check for more bufs coming from webserver (do this all in a loop until websrv_tx_ring is empty)
    void *request_id;
    uintptr_t rx_buf;
    unsigned int buf_len;
    int error = dequeue_used(&websrv_tx_ring, &rx_buf, &buf_len, &request_id);

    if (error)
    {
        sel4cp_dbg_puts("Failed to dequeue used from websrv_tx_ring\n");
        return;
    }

    // The first byte of the buffer gives us the file operation they are going for
    int op = ((char *)rx_buf)[0];
    switch (op)
    {
    case SYS_STAT64:
        nfs_stat64_async(nfs, (char *)rx_buf + 1, nfs_stat64_async_cb, request_id);
        break;
    case SYS_OPEN:
        break;
    case SYS_READ:
        break;
    case SYS_OPENREADCLOSE:
        handle_openreadclose(request_id, rx_buf, buf_len);
        break;
    default:
        break;
    }

    error = enqueue_avail(&websrv_tx_ring, rx_buf, BUF_SIZE, NULL);
    if (error)
    {
        sel4cp_dbg_puts("Failed to enqueue avail to websrv_tx_ring\n");
        return;
    }
}

void notified(sel4cp_channel ch)
{
    switch (ch)
    {
    case LWIP_NFS_CH:
        if (!nfs_init)
        {
            init_post();
            nfs_init = true;
        }
        else if (!nfs_socket_connected)
        {
            nfs_socket_connected = true;
            nfs_service(nfs, 0);
        }
        else
        {
            if (nfs_service(nfs, poll_lwip_socket()))
            {
                sel4cp_dbg_puts("nfs_service failed\n");
            }
        }
        break;
    case WEBSRV_CH:
        handle_webserver_request();
        break;
    case TIMER_CH:
        if (nfs_socket_connected)
        {
            if (nfs_service(nfs, poll_lwip_socket()))
            {
                sel4cp_dbg_puts("timer: nfs_service failed\n");
            }
        }
        break;
    default:
        sel4cp_dbg_puts("Got notification from unknown channel\n");
        break;
    }
}

void init(void)
{
    sel4cp_dbg_puts("init: starting nfs client\n");
    syscalls_init();

    nfs_send_to_lwip = __nfs_send_to_lwip;
    nfs_recv_from_lwip = __nfs_recv_from_lwip;
    nfs_close_lwip_sock = __nfs_close_lwip_sock;

    ring_init(&lwip_rx_ring, (ring_buffer_t *)rx_nfs_avail, (ring_buffer_t *)rx_nfs_used, NULL, 0);
    ring_init(&lwip_tx_ring, (ring_buffer_t *)tx_nfs_avail, (ring_buffer_t *)tx_nfs_used, NULL, 0);

    ring_init(&websrv_rx_ring, (ring_buffer_t *)rx_nfs_websrv_avail, (ring_buffer_t *)rx_nfs_websrv_used, NULL, 1);
    ring_init(&websrv_tx_ring, (ring_buffer_t *)tx_nfs_websrv_avail, (ring_buffer_t *)tx_nfs_websrv_used, NULL, 1);

    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        enqueue_avail(&websrv_rx_ring, shared_nfs_websrv_vaddr + (i * BUF_SIZE), BUF_SIZE, NULL);
    }

    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        enqueue_avail(&websrv_tx_ring, shared_nfs_websrv_vaddr + (BUF_SIZE * (i + NUM_BUFFERS)), BUF_SIZE, NULL);
    }

    nfs_openreadclose_data = malloc(sizeof(nfs_openreadclose_data_t *) * max_open_files);
    for (int i = 0; i < max_open_files; i++)
        nfs_openreadclose_data[i] = NULL;
}
