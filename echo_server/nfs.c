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

#define SERVER "10.13.1.90"
// #define EXPORT "/export/home/imx8mm"
// #define EXPORT "/System/Volumes/Data/Users/jasperdifrancesco/export/imx8mm"
#define EXPORT "/Users/jasperdifrancesco/export"

#define NFSFILE "foo"
#define NFSFILE2 "otherfile"

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

pid_t my_pid = NFS_PID;
extern socket_send_t nfs_send_to_lwip;
extern socket_recv_t nfs_recv_from_lwip;
extern socket_close_t nfs_close_lwip_sock;

nfs_open_file_t nfs_open_files[NUM_OPEN_FILES] = {0};

struct pollfd pfds[2]; /* nfs:0  mount:1 */
struct nfs_context *nfs;

// TODO remove
struct nfsfh *nfsfh = NULL;

void nfs_close_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data);

void write_bright_green(const char *str)
{
    sel4cp_dbg_puts("\033[32m");
    sel4cp_dbg_puts(str);
    sel4cp_dbg_puts("\033[0m");
}

void nfs_null_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    sel4cp_dbg_puts("nfs_null_cb\n");
}

void labelnum(char *s, uint64_t a);
void nfs_write_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    sel4cp_dbg_puts("nfs_write_async_cb\n");
    labelnum("nfs_write_async_cb status: ", status);
    if (status < 0)
    {
        sel4cp_dbg_puts("nfs_write_async_cb: failed to write to nfs file\n");
        if (nfs_get_error(nfs) != NULL)
        {
            sel4cp_dbg_puts("nfs_write_async_cb: error: ");
            sel4cp_dbg_puts(nfs_get_error(nfs));
            sel4cp_dbg_puts("\n");
        }
    }
    else
    {
        sel4cp_dbg_puts("nfs_write_async_cb: wrote to nfs file\n");
    }
    nfs_close_async(nfs, nfsfh, nfs_null_cb, NULL);
}

void nfs_read_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    sel4cp_dbg_puts("nfs_read_async_cb\n");
    labelnum("nfs_write_async_cb status: ", status);
    if (status < 0)
    {
        sel4cp_dbg_puts("nfs_read_async_cb: failed to read from nfs file\n");
        if (nfs_get_error(nfs) != NULL)
        {
            sel4cp_dbg_puts("nfs_read_async_cb: error: ");
            sel4cp_dbg_puts(nfs_get_error(nfs));
            sel4cp_dbg_puts("\n");
        }
        if (data != NULL)
        {
            sel4cp_dbg_puts("nfs_read_async_cb: data: ");
            sel4cp_dbg_puts((char *)data);
            sel4cp_dbg_puts("\n");
        }
    }
    else
    {
        sel4cp_dbg_puts("nfs_read_async_cb: read from nfs file: ");
        sel4cp_dbg_puts((char *)data);
        sel4cp_dbg_puts("\n");
    }
}

void nfs_creat_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    sel4cp_dbg_puts("nfs_creat_async_cb\n");
    nfsfh = (struct nfsfh *)data;
    if (status != 0)
    {
        sel4cp_dbg_puts("nfs_creat_async_cb: failed to create nfs file\n");
        if (nfs_get_error(nfs) != NULL)
        {
            sel4cp_dbg_puts("nfs_creat_async_cb: error: ");
            sel4cp_dbg_puts(nfs_get_error(nfs));
            sel4cp_dbg_puts("\n");
        }
    }
    else
    {
        sel4cp_dbg_puts("nfs_creat_async_cb: nfs file created\n");
        nfs_write_async(nfs, nfsfh, 4, "test", nfs_write_async_cb, NULL);
    }
}

// void nfs_close_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
// {
//     sel4cp_dbg_puts("nfs_close_async_cb\n");
//     if (status != 0)
//     {
//         sel4cp_dbg_puts("nfs_close_async_cb: failed to close nfs file\n");
//     }
//     else
//     {
//         sel4cp_dbg_puts("nfs_close_async_cb: nfs file closed\n");
//     }
//     nfs_creat_async(nfs, NFSFILE2, O_CREAT | O_WRONLY, nfs_creat_async_cb, NULL);
// }

void nfs_open_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    sel4cp_dbg_puts("nfs_open_async_cb\n");
    if (status != 0)
    {
        sel4cp_dbg_puts("nfs_open_async_cb: failed to open nfs file\n");
        if (nfs_get_error(nfs) != NULL)
        {
            sel4cp_dbg_puts("nfs_open_async_cb: error: ");
            sel4cp_dbg_puts(nfs_get_error(nfs));
            sel4cp_dbg_puts("\n");
        }

        if (data != NULL)
        {
            sel4cp_dbg_puts("nfs_open_async_cb: data: ");
            sel4cp_dbg_puts((char *)data);
            sel4cp_dbg_puts("\n");
        }
    }
    else
    {
        sel4cp_dbg_puts("nfs_open_async_cb: nfs file opened\n");
        nfsfh = (struct nfsfh *)data;
        // nfs_close_async(nfs, nfsfh, nfs_close_async_cb, NULL);
        // nfs_write_async(nfs, nfsfh, 29, "Writing with a longer string", nfs_write_async_cb, NULL);
        nfs_read_async(nfs, nfsfh, 4, nfs_read_async_cb, NULL);
    }
}

void nfs_open_read_async_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    sel4cp_dbg_puts("nfs_open_async_cb\n");
    if (status != 0)
    {
        sel4cp_dbg_puts("nfs_open_async_cb: failed to open nfs file\n");
        if (nfs_get_error(nfs) != NULL)
        {
            sel4cp_dbg_puts("nfs_open_async_cb: error: ");
            sel4cp_dbg_puts(nfs_get_error(nfs));
            sel4cp_dbg_puts("\n");
        }
    }
    else
    {
        sel4cp_dbg_puts("nfs_open_async_cb: nfs file opened\n");
        nfsfh = (struct nfsfh *)data;
        // nfs_close_async(nfs, nfsfh, nfs_close_async_cb, NULL);
        nfs_read_async(nfs, nfsfh, 29, nfs_read_async_cb, NULL);
    }
}

void nfs_do()
{
    sel4cp_dbg_puts("nfs_do\n");
    if (nfs_open_async(nfs, NFSFILE2, O_RDONLY, nfs_open_read_async_cb, NULL) != 0)
    // if (nfs_creat_async(nfs, NFSFILE2, MASK(9), nfs_open_async_cb, NULL))
    // if (nfs_open_async(nfs, NFSFILE2, O_RDWR | O_CREAT, nfs_open_async_cb, NULL) != 0)
    {
        printf("Failed to start async nfs stat\n");
        exit(10);
    }
}

void nfs_fake_close(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    sel4cp_dbg_puts("nfs_fake_close\n");
    // (void(*) void *)private_data();
    ((void (*)())private_data)(NULL);
}

void nfs_fake_open(int status, struct nfs_context *nfs, void *data, void *private_data)
{
    nfs_close_async(nfs, data, nfs_fake_close, private_data);
}

void nfs_connect_cb(int err, struct nfs_context *nfs_ctx, void *data, void *private_data)
{
    sel4cp_dbg_puts("nfs_connect_cb\n");
    if (err != 0)
    {
        sel4cp_dbg_puts("nfs_connect_cb: failed to connect to nfs server\n");
        sel4cp_dbg_puts(nfs_get_error(nfs));
        sel4cp_dbg_puts("\n");
        return;
    }
    sel4cp_dbg_puts("nfs_connect_cb: connected to nfs server\n");
    return;
    // if (nfs_creat_async(nfs, NFSFILE2, MASK(9), nfs_creat_async_cb, NULL) != 0)
    if (nfs_open_async(nfs, NFSFILE2, O_RDWR, nfs_open_async_cb, NULL) != 0)
    {
        printf("Failed to start async nfs stat\n");
        exit(10);
    }
}

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
    sel4cp_dbg_puts("init: connecting to nfs server\n");
}

void write_num(int num)
{
    char buf[10];
    int i = 0;
    while (num > 0)
    {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    for (int j = i - 1; j >= 0; j--)
    {
        sel4cp_dbg_putc(buf[j]);
    }
}

void char_to_hex(char c, char *buf)
{
    char *hex = "0123456789ABCDEF";
    buf[0] = hex[(c >> 4) & 0xF];
    buf[1] = hex[c & 0xF];
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

void print_bright_green_buf(uintptr_t buf, int len)
{
    sel4cp_dbg_puts("\033[1;32m");
    print_buf_len(buf, len);
    sel4cp_dbg_puts("\033[0m\n");
}

void writenum(int num)
{
    char buf[10];
    int i = 0;
    while (num > 0)
    {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    for (int j = i - 1; j >= 0; j--)
    {
        sel4cp_dbg_putc(buf[j]);
    }
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

        enqueue_used(&lwip_tx_ring, tx_buf, bytes_to_write, (uintptr_t)fd);
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
        // sel4cp_dbg_puts("nfs_recv_from_lwip: while loop\n");
        buf[bytes_read] = nfs_socket_buf[nfs_socket_read];
        nfs_socket_read++;
        nfs_socket_read %= BUF_SIZE;
        bytes_read++;
    }

    while (bytes_read < len)
    {
        // sel4cp_dbg_puts("nfs_recv_from_lwip: while loop\n");
        // sel4cp_dbg_puts("Copying mpybuf to ringbuf");
        // label_num("bytes_read: ", bytes_read);
        // label_num("len: ", len);
        void *rx_cookie;
        uintptr_t rx_buf;
        unsigned int temp_len;

        // if (ring_empty(lwip_rx_ring.used_ring))
        // {
        //     break;
        // }

        int error = dequeue_used(&lwip_rx_ring, &rx_buf, &temp_len, &rx_cookie);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue used from lwip_rx_ring\n");
            return 0;
        }

        // Copy from rx_buf to buf
        unsigned int bytes_to_read = MIN((len - bytes_read), BUF_SIZE);
        for (unsigned int i = 0; i < bytes_to_read; i++)
        {
            buf[bytes_read + i] = ((char *)rx_buf)[i];
        }
        bytes_read += bytes_to_read;

        // More in the buffer than NFS is requesting. Fill our socket_buf with what's left
        if (temp_len > bytes_to_read)
        {
            for (unsigned int i = 0; i < temp_len - bytes_to_read; i++)
            {
                nfs_socket_buf[nfs_socket_write] = ((char *)rx_buf)[bytes_to_read + i];
                nfs_socket_write++;
                nfs_socket_write %= BUF_SIZE;
            }
        }

        enqueue_avail(&lwip_rx_ring, rx_buf, BUF_SIZE, 0);
    }

    return bytes_read;
}

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
            sel4cp_dbg_puts("Failed to dequeue used from lwip_rx_ring\n");
            return 0;
        }

        enqueue_avail(&lwip_rx_ring, rx_buf, BUF_SIZE, 0);
    }
}

int poll_lwip_socket(void)
{
    if (!nfs_socket_connected)
        return 0;

    int ret = 0;
    int events = nfs_which_events(nfs);
    // labelnum("events: ", events);
    if ((events & POLLOUT) && !ring_empty(lwip_tx_ring.avail_ring))
    {
        ret |= POLLOUT;
    }
    if ((events & POLLIN) && (!ring_empty(lwip_rx_ring.used_ring) || nfs_socket_read != nfs_socket_write))
    {
        ret |= POLLIN;
    }
    // labelnum("ret: ", ret);
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
        sel4cp_dbg_puts("nfs_stat_async_cb: failed to stat file\n");
        sel4cp_dbg_puts(nfs_get_error(nfs));
        sel4cp_dbg_puts("\n");
    }
    else
    {
        struct nfs_stat_64 *st = (struct nfs_stat_64 *)data;
        sel4cp_dbg_puts("nfs_stat_async_cb: stat file success\n");
        labelnum("st->nfs_size: ", st->nfs_size);
        labelnum("st->nfs_mode: ", st->nfs_mode);
        labelnum("st->nfs_nlink: ", st->nfs_nlink);
        labelnum("st->nfs_uid: ", st->nfs_uid);
        labelnum("st->nfs_gid: ", st->nfs_gid);
        labelnum("st->nfs_atime: ", st->nfs_atime);
        labelnum("st->nfs_mtime: ", st->nfs_mtime);
        labelnum("st->nfs_ctime: ", st->nfs_ctime);
        labelnum("st->nfs_dev: ", st->nfs_dev);
        labelnum("st->nfs_ino: ", st->nfs_ino);
        labelnum("st->nfs_rdev: ", st->nfs_rdev);
        labelnum("st->nfs_blksize: ", st->nfs_blksize);
        labelnum("st->nfs_blocks: ", st->nfs_blocks);

        mpy_stat_t mpy_stat;

        mpy_stat.file_size = (uint32_t)st->nfs_size;
        mtime_to_dos_date_time(st->nfs_mtime, &mpy_stat.last_mod_date, &mpy_stat.last_mod_time);
        mpy_stat.is_dir = S_ISDIR(st->nfs_mode);

        void *continuation_id;
        uintptr_t rx_buf;
        unsigned int buf_len;

        int error = dequeue_avail(&websrv_rx_ring, &rx_buf, &buf_len, &continuation_id);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue avail from websrv_tx_ring\n");
            return;
        }

        memcpy((void *)rx_buf, st, sizeof(mpy_stat_t));
        enqueue_used(&websrv_tx_ring, rx_buf, sizeof(mpy_stat_t), (void *)continuation_id);
        sel4cp_notify(WEBSRV_CH);
    }
}

void handle_webserver_request(void)
{
    // Only worry about receiving one at a time for now

    sel4cp_dbg_puts("handle_webserver_request\n");
    void *continuation_id;
    uintptr_t rx_buf;
    unsigned int buf_len;
    dequeue_used(&websrv_tx_ring, &rx_buf, &buf_len, &continuation_id);

    // The first byte of the buffer gives us the file operation they are going for
    int op = ((char *)rx_buf)[0];
    switch (op)
    {
    case SYS_STAT64:
        nfs_stat64_async(nfs, (char *)rx_buf + 1, nfs_stat64_async_cb, continuation_id);
        break;
    case SYS_OPEN:
        break;
    case SYS_READ:
        break;
    default:
        break;
    }

    enqueue_avail(&websrv_tx_ring, rx_buf, BUF_SIZE, NULL);
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
            nfs_service(nfs, poll_lwip_socket());
        }
        break;
    case WEBSRV_CH:
        sel4cp_dbg_puts("Got notification from websrv\n");
        // Requesting a file
        handle_webserver_request();
        break;
    case TIMER_CH:
        if (nfs_socket_connected)
        {
            nfs_service(nfs, poll_lwip_socket());
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

    for (int i = 0; i < NUM_BUFFERS - 1; i++)
    {
        enqueue_avail(&websrv_rx_ring, shared_nfs_websrv_vaddr + (i * BUF_SIZE), BUF_SIZE, NULL);
    }

    for (int i = 0; i < NUM_BUFFERS - 1; i++)
    {
        enqueue_avail(&websrv_tx_ring, shared_nfs_websrv_vaddr + (BUF_SIZE * (i * NUM_BUFFERS)), BUF_SIZE, NULL);
    }
}