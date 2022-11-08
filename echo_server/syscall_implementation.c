#include <syscall_implementation.h>

extern void *__sysinfo;

long sel4_vsyscall(long sysnum, ...)
{
    sel4cp_dbg_puts("sel4_vsyscall: \n");
    return 0;
}

void syscalls_init(void)
{
    __sysinfo = sel4_vsyscall;
}