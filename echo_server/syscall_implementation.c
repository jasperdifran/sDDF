#include <syscall_implementation.h>
#include <stdarg.h>
#include <bits/syscall.h>
#include <stdio.h>
#include <errno.h>
#include <sel4cp.h>

#define MUSLC_HIGHEST_SYSCALL SYS_pkey_free
#define MUSLC_NUM_SYSCALLS (MUSLC_HIGHEST_SYSCALL + 1)
#define MAP_ANON 0x20
#define MAP_ANONYMOUS MAP_ANON

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
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);

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
    // ZF_LOGF("not implemented");
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

long sys_getpid(va_list ap)
{
    return my_pid;
}

void syscalls_init(void)
{
    __sysinfo = sel4_vsyscall;
    syscall_table[__NR_brk] = (muslcsys_syscall_t)sys_brk;
    syscall_table[__NR_write] = (muslcsys_syscall_t)sys_write;
    syscall_table[__NR_mmap] = (muslcsys_syscall_t)sys_mmap;
    syscall_table[__NR_getpid] = (muslcsys_syscall_t)sys_getpid;
    // muslcsys_install_syscall(__NR_gettid, sys_gettid);
}

void labelnum(long num)
{
    char buf[20] = {0};
    int i = 0;
    for (i = 0; i < 20; i++)
    {
        buf[i] = num % 10 + '0';
        num /= 10;
    }
    // Reverse buf to get the number in the right order
    for (int j = 0; j < i; j++)
    {
        sel4cp_dbg_putc(buf[i - j - 1]);
    }
}

void debug_error(long num)
{
    sel4cp_dbg_puts("Error doing syscall: ");
    labelnum(num);
    sel4cp_dbg_puts("\n");
}

long sel4_vsyscall(long sysnum, ...)
{

    sel4cp_dbg_putc('\%');
    sel4cp_dbg_putc(my_pid + '0');
    labelnum(sysnum);
    sel4cp_dbg_puts("\%\n");
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
    sel4cp_dbg_puts("\%Done syscall\%\n");
    return ret;
}
