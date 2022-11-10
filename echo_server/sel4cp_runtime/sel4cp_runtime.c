#include <auxv.h>

// Declaration for the function we use to startup musl
void _start_c(long *p);

void _sel4cp_start_c(long *p)
{
    // Place for writing argc, argv, envp and auxv to the stack

    // Call the musl startup function
    _start_c(p);
}