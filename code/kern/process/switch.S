#include <riscv.h>

#lab4 function3 2411353
# switch_to - save the callee-saved context of "from" into its context
#             structure and restore the context of "to"
#   a0: struct context *from
#   a1: struct context *to
.text
.globl switch_to
switch_to:
    # save from's context (ra, sp, s0-s11, same order as struct context)
    STORE ra,   0 * REGBYTES(a0)
    STORE sp,   1 * REGBYTES(a0)
    STORE s0,   2 * REGBYTES(a0)
    STORE s1,   3 * REGBYTES(a0)
    STORE s2,   4 * REGBYTES(a0)
    STORE s3,   5 * REGBYTES(a0)
    STORE s4,   6 * REGBYTES(a0)
    STORE s5,   7 * REGBYTES(a0)
    STORE s6,   8 * REGBYTES(a0)
    STORE s7,   9 * REGBYTES(a0)
    STORE s8,  10 * REGBYTES(a0)
    STORE s9,  11 * REGBYTES(a0)
    STORE s10, 12 * REGBYTES(a0)
    STORE s11, 13 * REGBYTES(a0)

    # restore to's context; ret jumps to to->ra (forkret for a new thread)
    LOAD ra,   0 * REGBYTES(a1)
    LOAD sp,   1 * REGBYTES(a1)
    LOAD s0,   2 * REGBYTES(a1)
    LOAD s1,   3 * REGBYTES(a1)
    LOAD s2,   4 * REGBYTES(a1)
    LOAD s3,   5 * REGBYTES(a1)
    LOAD s4,   6 * REGBYTES(a1)
    LOAD s5,   7 * REGBYTES(a1)
    LOAD s6,   8 * REGBYTES(a1)
    LOAD s7,   9 * REGBYTES(a1)
    LOAD s8,  10 * REGBYTES(a1)
    LOAD s9,  11 * REGBYTES(a1)
    LOAD s10, 12 * REGBYTES(a1)
    LOAD s11, 13 * REGBYTES(a1)

    ret
