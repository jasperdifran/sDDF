/*
 * Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

.section .text
.global _start
_start:
    /* Required for correct return address */
    mov fp, #0
    mov lr, #0 

    /* Setup stack pointer */
    ldr x0, =_stack + 0xff0
	mov sp, x0

    /* Set argc */
    mov x1, #0
    str x1, [sp]

    /* Set argv */
    str x1, [sp, #8]

    /* Set envp */
    str x1, [sp, #16]

    /* Start in c land */
	bl _sel4cp_start_c

	/* should not return */
1:
	b 1b
