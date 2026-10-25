#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>
#include <mmu.h>

/*
 * SLUB-style kernel object allocator: the second memory-management layer on
 * top of the page allocator (alloc_page/free_page).
 *
 * Size classes (rounded up):
 *     8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 bytes
 * Requests from one class are served by exactly one kmem_cache.
 * Every returned object is aligned to its class size (at least 8 bytes).
 *
 * API/error semantics (kept intentionally simple and consistent):
 *   slub_init()             must be called once, after pmm_init().
 *   kmalloc(0)              returns NULL.
 *   kmalloc(size > PGSIZE)  returns NULL.
 *   kmalloc() before init   panics.
 *   kmalloc() exhaustion    returns NULL.
 *   kfree(NULL)             panics (NULL is not an object returned by kmalloc).
 *   kfree(other invalid)    panics (outside managed memory, not in a slab,
 *                           misaligned, corrupted page metadata).
 *   kfree(free object)      panics (double free).
 */
void slub_init(void);

void *kmalloc(size_t size);
void kfree(void *ptr);

#endif /* !__KERN_MM_SLUB_H__ */
