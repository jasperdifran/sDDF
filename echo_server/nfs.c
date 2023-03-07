#include <sel4cp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <nfsc/libnfs.h>
#include <nfsc/libnfs-raw.h>
#include <sys/types.h>
#include <syscall_implementation.h>
#include <poll.h>

#include "shared_ringbuffer.h"

#include "shared_ringbuffer.h"

#include "util.h"

#define MIN(a, b) (a < b) ? a : b

#define WEBSRV_CH 7
#define LWIP_NFS_CH 8
#define TIMER_CH 10

#define MY_PID NFS_PID

#define NUM_BUFFERS 512
#define BUF_SIZE 2048

#define SERVER "10.1.1.27"
#define EXPORT "/VIRTUAL"
#define NFSFILE "/BOOKS/Classics/Dracula.djvu"
#define NFSDIR "/BOOKS/Classics/"

pid_t my_pid = NFS_PID;
extern socket_send nfs_send_to_lwip;
extern socket_recv nfs_recv_from_lwip;

struct client
{
    char server[128];
    char export[128];
    uint32_t mount_port;
    struct nfsfh *nfsfh;
    int is_finished;
};

struct rpc_context *rpc;
struct nfs_context *nfs;
struct client client = {
    .server = SERVER,
    .export = EXPORT,
    .mount_port = 0,
    .nfsfh = NULL,
    .is_finished = 0,
};

struct pollfd pfds[2]; /* nfs:0  mount:1 */

// struct nfs_context *nfsContext = NULL;

// void nfs_mount_cb(int err, struct nfs_context *nfs, void *data, void *private_data)
// {
//     if (err != 0)
//     {
//         sel4cp_dbg_puts("nfs_mount_cb: failed to mount nfs share\n");
//         return;
//     }
//     sel4cp_dbg_puts("nfs_mount_cb: nfs share mounted\n");
// }

void write_bright_green(const char *str)
{
    sel4cp_dbg_puts("\033[32m");
    sel4cp_dbg_puts(str);
    sel4cp_dbg_puts("\033[0m");
}

void nfs_connect_cb(int err, struct nfs_context *nfs_ctx, void *data, void *private_data)
{
    sel4cp_dbg_puts((char *)data);
    if (err != 0)
    {
        sel4cp_dbg_puts("nfs_connect_cb: failed to connect to nfs server\n");
        return;
    }
    sel4cp_dbg_puts("nfs_connect_cb: connected to nfs server\n");
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

// void write_pointer_hex(void *ptr);

void init_post(void)
{
    sel4cp_dbg_puts("init_post: starting nfs client\n");
    nfs = nfs_init_context();

    if (nfs == NULL)
    {
        sel4cp_dbg_puts("init: failed to init nfs context\n");
        return;
    }

    if (nfs_mount_async(nfs, client.server, client.export, nfs_connect_cb, &client) != 0)
    {
        sel4cp_dbg_puts("init: failed to connect to nfs server\n");
        return;
    }
    sel4cp_dbg_puts("init: connected to nfs server\n");

    sel4cp_dbg_puts("Init nfs pd\n");

    // write_pointer_hex(nfs_connect_cb);
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

// void labelnum(const char *label, int num)
// {
//     sel4cp_dbg_puts(label);
//     write_num(num);
//     sel4cp_dbg_puts("\n");
// }

static void __nfs_send_to_lwip(void *buffer, size_t len)
{

    sel4cp_dbg_puts("nfs_send_to_lwip: \n");
    char *buf = (char *)buffer;
    unsigned int bytes_written = 0;

    while (bytes_written < len)
    {
        sel4cp_dbg_puts("nfs_send_to_lwip: while loop\n");
        // sel4cp_dbg_puts("Copying mpybuf to ringbuf");
        // label_num("bytes_written: ", bytes_written);
        // label_num("len: ", len);
        void *tx_cookie;
        uintptr_t tx_buf;
        unsigned int temp_len;

        if (ring_empty(lwip_tx_ring.avail_ring))
        {
            // sel4cp_dbg_puts("lwip_tx_ring.avail_ring is empty");
            sel4cp_notify(LWIP_NFS_CH);
        }
        // while (ring_empty(&lwip_tx_ring))
        //     ;
        int error = dequeue_avail(&lwip_tx_ring, &tx_buf, &temp_len, &tx_cookie);
        if (error)
        {
            sel4cp_dbg_puts("Failed to dequeue avail from lwip_tx_ring\n");
            return;
        }

        unsigned int bytes_to_write = MIN((len - bytes_written), BUF_SIZE);
        labelnum("bytes_to_write: ", bytes_to_write);
        for (unsigned int i = 0; i < bytes_to_write; i++)
        {
            ((char *)tx_buf)[i] = buf[bytes_written + i];
        }
        bytes_written += bytes_to_write;

        enqueue_used(&lwip_tx_ring, tx_buf, bytes_to_write, 0);
    }
    sel4cp_notify(LWIP_NFS_CH);
    sel4cp_dbg_puts("nfs_send_to_lwip: done\n");
}

int nfs_socket_read = 0, nfs_socket_write = 0;
char *nfs_socket_buf[BUF_SIZE];

static size_t __nfs_recv_from_lwip(void *buffer, size_t len)
{
    labelnum("nfs_recv_from_lwip: len: ", len);
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
        labelnum("bytes_to_read: ", bytes_to_read);
        for (unsigned int i = 0; i < bytes_to_read; i++)
        {
            buf[bytes_read + i] = ((char *)rx_buf)[i];
        }
        bytes_read += bytes_to_read;

        // More in the buffer than NFS is requesting. Fill our socket_buf with what's left
        if (temp_len > bytes_to_read)
        {
            sel4cp_dbg_puts("More in the buffer than NFS is requesting. Fill our socket_buf with what's left\n");
            labelnum("temp_len - bytes_to_read: ", temp_len - bytes_to_read);
            for (unsigned int i = 0; i < temp_len - bytes_to_read; i++)
            {
                nfs_socket_buf[nfs_socket_write] = ((char *)rx_buf)[bytes_to_read + i];
                nfs_socket_write++;
                nfs_socket_write %= BUF_SIZE;
            }
            labelnum("nfs_socket_write: ", nfs_socket_write);
            labelnum("nfs_socket_read: ", nfs_socket_read);
        }

        enqueue_avail(&lwip_rx_ring, rx_buf, BUF_SIZE, 0);
        // print_bright_magenta_buf((uintptr_t)rx_buf, len);
    }

    print_bright_magenta_buf((uintptr_t)buf, len);
    return bytes_read;
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
            write_bright_green("NFS socket connected\n");
            nfs_socket_connected = true;
            nfs_service(nfs, 0);
        }
        else
        {
            write_bright_green("NFS RX...\n");
            nfs_service(nfs, poll_lwip_socket());
        }
        break;
    case WEBSRV_CH:
        sel4cp_dbg_puts("Got notification from websrv\n");
        // Requesting a file
        break;
    case TIMER_CH:
        if (nfs_socket_connected)
        {
            write_bright_green("NFS timer ticked\n");
            nfs_service(nfs, poll_lwip_socket());
        }

        break;
    default:
        sel4cp_dbg_puts("Got notification from unknown channel\n");
        break;
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

void init(void)
{
    sel4cp_dbg_puts("init: starting nfs client\n");
    syscalls_init();

    nfs_send_to_lwip = __nfs_send_to_lwip;
    nfs_recv_from_lwip = __nfs_recv_from_lwip;

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