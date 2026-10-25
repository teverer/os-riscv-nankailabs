#ifndef __KERN_MM_SLUB_CHECK_H__
#define __KERN_MM_SLUB_CHECK_H__

/*
 * Unified black-box test entry for the SLUB allocator.
 *
 * check_slub() runs checks 1-6 and 8 and prints a summary.  Check 7
 * (illegal-use detection) deliberately panics, so it is a separate minimal
 * stage; run exactly one case per boot:
 *
 *   0: kfree() the same live-slab object twice
 *   1: kfree() of a kernel stack address
 *   2: kfree(NULL)
 *   3: kfree() of a misaligned pointer inside a slab object
 */
void check_slub(void);
void check_slub_bad_use(int which);

#endif /* !__KERN_MM_SLUB_CHECK_H__ */
