#include <stdarg.h>
#include <bits/syscall.h>
#include <stdio.h>
#include <errno.h>
#include <sel4cp.h>
#include <sys/time.h>
#include <time.h>
#include <autoconf.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/uio.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <syscall_implementation.h>
#include <syscall_uart.h>
#include <sys/syscall.h>

#define MUSLC_HIGHEST_SYSCALL SYS_pkey_free
#define MUSLC_NUM_SYSCALLS (MUSLC_HIGHEST_SYSCALL + 1)
#define MAP_ANON 0x20
#define MAP_ANONYMOUS MAP_ANON

#define LWIP_CH 8
#define SEL4CP_SOCKET 0
#define SEL4CP_SOCKET_CONNECT 1

#define STDOUT_FD 1
#define STDERR_FD 2
#define LWIP_FD_START 3

typedef long (*muslcsys_syscall_t)(va_list);

extern void *__sysinfo;
extern pid_t my_pid;

static muslcsys_syscall_t syscall_table[MUSLC_NUM_SYSCALLS] = {0};

long sel4_vsyscall(long sysnum, ...);

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/*
 * Statically allocated morecore area.
 *
 * This is rather terrible, but is the simplest option without a
 * huge amount of infrastructure.
 */
#define MORECORE_AREA_BYTE_SIZE 0x100000
char morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Pointer to free space in the morecore area. */
static uintptr_t morecore_base = (uintptr_t)&morecore_area;
static uintptr_t morecore_top = (uintptr_t)&morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Actual morecore implementation
   returns 0 if failure, returns newbrk if success.
*/

int errnovar = 0;

int *__error()
{
    return &errnovar;
}

int *___errno_location(void)
{
    return __error();
}

void print_num(uint64_t num)
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

void labelnum(char *s, uint64_t n)
{
    sel4cp_dbg_puts(s);
    print_num(n);
    sel4cp_dbg_puts("\n");
}

static size_t output(void *data, size_t count)
{
    imx8mm_output_strn((char *)data, count);
}

long sys_brk(va_list ap)
{

    uintptr_t ret;
    uintptr_t newbrk = va_arg(ap, uintptr_t);

    /*if the newbrk is 0, return the bottom of the heap*/
    if (!newbrk)
    {
        ret = morecore_base;
    }
    else if (newbrk < morecore_top && newbrk > (uintptr_t)&morecore_area[0])
    {
        ret = morecore_base = newbrk;
    }
    else
    {
        ret = 0;
    }

    return ret;
}

long sys_mmap(va_list ap)
{
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    if (length == 0)
        length = 0x1000; // Default to 4K
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);
    (void)fd, (void)offset, (void)prot, (void)addr;

    if (flags & MAP_ANONYMOUS)
    {
        /* Check that we don't try and allocate more than exists */
        if (length > morecore_top - morecore_base)
        {
            return -ENOMEM;
        }
        /* Steal from the top */
        morecore_top -= length;
        return morecore_top;
    }
    sel4cp_dbg_puts("sys_mmap not implemented");
    return -ENOMEM;
}

long sys_write(va_list ap)
{
    int fd = va_arg(ap, int);
    const void *buf = va_arg(ap, const void *);
    size_t count = va_arg(ap, size_t);

    if (fd == 1 || fd == 2)
    {
        sel4cp_dbg_puts(buf);
        return count;
    }
    else
    {
        return -1;
    }
}

long sys_clock_gettime(va_list ap)
{
    clockid_t clk_id = va_arg(ap, clockid_t);
    struct timespec *tp = va_arg(ap, struct timespec *);

    uint64_t rtc = rtc_now_ms();

    tp->tv_sec = rtc / 1000;
    tp->tv_nsec = (rtc % 1000) * 1000000;

    return 0;
}

long sys_getpid(va_list ap)
{
    return my_pid;
}

long sys_ioctl(va_list ap)
{
    int fd = va_arg(ap, int);
    int request = va_arg(ap, int);
    (void)request;
    /* muslc does some ioctls to stdout, so just allow these to silently
       go through */
    if (fd == STDOUT_FD)
    {
        return 0;
    }

    sel4cp_dbg_puts("io ctl not implemented");
    return 0;
}

long sys_writev(va_list ap)
{
    int fildes = va_arg(ap, int);
    struct iovec *iov = va_arg(ap, struct iovec *);
    int iovcnt = va_arg(ap, int);

    long long sum = 0;
    ssize_t ret = 0;

    /* The iovcnt argument is valid if greater than 0 and less than or equal to IOV_MAX. */
    if (iovcnt <= 0 || iovcnt > IOV_MAX)
    {
        return -EINVAL;
    }

    /* The sum of iov_len is valid if less than or equal to SSIZE_MAX i.e. cannot overflow
       a ssize_t. */
    for (int i = 0; i < iovcnt; i++)
    {
        sum += (long long)iov[i].iov_len;
        if (sum > SSIZE_MAX)
        {
            return -EINVAL;
        }
    }

    /* If all the iov_len members in the array are 0, return 0. */
    if (!sum)
    {
        return 0;
    }

    /* Write the buffer to console if the fd is for stdout or stderr. */
    if (fildes == STDOUT_FD || fildes == STDERR_FD)
    {
        for (int i = 0; i < iovcnt; i++)
        {
            ret += output(iov[i].iov_base, iov[i].iov_len);
        }
    }
    else
        sel4cp_dbg_puts("writev not implemented");

    return ret;
}

long sys_openat(va_list ap)
{
    (void)ap;
    return ENOSYS;
}

long sys_socket(va_list ap)
{
    int domain = va_arg(ap, int);
    int type = va_arg(ap, int);
    int protocol = va_arg(ap, int);

    sel4cp_msginfo msg = sel4cp_msginfo_new(0, 4);
    sel4cp_mr_set(0, SEL4CP_SOCKET);
    sel4cp_mr_set(1, domain);
    sel4cp_mr_set(2, type);
    sel4cp_mr_set(3, protocol);

    sel4cp_msginfo ret = sel4cp_ppcall(LWIP_CH, msg);
    int new_sd = sel4cp_mr_get(0);
    labelnum("socket: ", new_sd);
    return (long)new_sd;
}

long sys_fcntl(va_list ap)
{
    return;
    int fd = va_arg(ap, int);
    int cmd = va_arg(ap, int);
    int arg = va_arg(ap, int);

    sel4cp_msginfo msg = sel4cp_msginfo_new(0, 4);
    sel4cp_mr_set(0, 2);
    sel4cp_mr_set(1, fd);
    sel4cp_mr_set(2, cmd);
    sel4cp_mr_set(3, arg);

    sel4cp_msginfo ret = sel4cp_ppcall(LWIP_CH, msg);
    int new_sd = sel4cp_mr_get(0);
    labelnum("fcntl: ", new_sd);
    return (long)new_sd;
}

long sys_bind(va_list ap)
{
    return;
    int sockfd = va_arg(ap, int);
    const struct sockaddr *addr = va_arg(ap, const struct sockaddr *);
    socklen_t addrlen = va_arg(ap, socklen_t);

    sel4cp_msginfo msg = sel4cp_msginfo_new(0, 4);
    sel4cp_mr_set(0, 2);
    sel4cp_mr_set(1, sockfd);
    sel4cp_mr_set(2, (seL4_Word)addr);
    sel4cp_mr_set(3, addrlen);

    sel4cp_msginfo ret = sel4cp_ppcall(LWIP_CH, msg);
    int new_sd = sel4cp_mr_get(0);
    labelnum("bind: ", new_sd);
    return (long)new_sd;
}

long sys_setsockopt(va_list ap)
{
    sel4cp_dbg_puts("setsockopt not implemented\n");
    return 0;
}

long sys_getsockopt(va_list ap)
{
    sel4cp_dbg_puts("getsockopt not implemented\n");
    return 0;
}

long sys_socket_connect(va_list ap)
{
    (void)ap;
    sel4cp_msginfo msg = sel4cp_msginfo_new(0, 1);
    sel4cp_mr_set(0, SEL4CP_SOCKET_CONNECT);
    sel4cp_msginfo ret = sel4cp_ppcall(LWIP_CH, msg);
    int val = sel4cp_mr_get(0);
    labelnum("socket_connect: ", val);
    return (long)val;
}

long sys_getuid(va_list ap)
{
    (void)ap;
    return 1;
}

long sys_getgid(va_list ap)
{
    (void)ap;
    return 1;
}

void syscalls_init(void)
{
    /* Timer init */
    rtc_enable();

    /* Syscall table init */
    __sysinfo = sel4_vsyscall;
    syscall_table[__NR_brk] = (muslcsys_syscall_t)sys_brk;
    syscall_table[__NR_write] = (muslcsys_syscall_t)sys_write;
    syscall_table[__NR_mmap] = (muslcsys_syscall_t)sys_mmap;
    syscall_table[__NR_getpid] = (muslcsys_syscall_t)sys_getpid;
    syscall_table[__NR_clock_gettime] = (muslcsys_syscall_t)sys_clock_gettime;
    syscall_table[__NR_ioctl] = (muslcsys_syscall_t)sys_ioctl;
    syscall_table[__NR_writev] = (muslcsys_syscall_t)sys_writev;
    syscall_table[__NR_openat] = (muslcsys_syscall_t)sys_openat;
    syscall_table[__NR_socket] = (muslcsys_syscall_t)sys_socket;
    syscall_table[__NR_fcntl] = (muslcsys_syscall_t)sys_fcntl;
    syscall_table[__NR_bind] = (muslcsys_syscall_t)sys_bind;
    syscall_table[__NR_connect] = (muslcsys_syscall_t)sys_socket_connect;
    syscall_table[__NR_getuid] = (muslcsys_syscall_t)sys_getuid;
    syscall_table[__NR_getgid] = (muslcsys_syscall_t)sys_getgid;
    syscall_table[__NR_setsockopt] = (muslcsys_syscall_t)sys_setsockopt;
    syscall_table[__NR_getsockopt] = (muslcsys_syscall_t)sys_setsockopt;
}

void debug_error(long num)
{
    labelnum("Error doing syscall: ", num);
    sel4cp_dbg_puts("\n");
}

int pthread_setcancelstate(int state, int *oldstate)
{
    (void)state;
    (void)oldstate;
    return 0;
}

long sel4_vsyscall(long sysnum, ...)
{
    va_list al;
    va_start(al, sysnum);
    muslcsys_syscall_t syscall;
    if (sysnum < 0 || sysnum >= ARRAY_SIZE(syscall_table))
    {
        // debug_error(sysnum);
        return -ENOSYS;
    }
    else
    {
        syscall = syscall_table[sysnum];
    }
    /* Check a syscall is implemented there */
    if (!syscall)
    {
        debug_error(sysnum);
        return -ENOSYS;
    }
    /* Call it */
    long ret = syscall(al);
    va_end(al);
    return ret;
}
