#include <assert.h>
#include <defs.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <slub.h>
#include <stdio.h>

/*
 * A compact SLUB-style allocator for a single-core teaching kernel.
 *
 * Every cache owns slabs of exactly one page.  A slab is described by the
 * struct Page of that page; no extra per-slab object is allocated.  The page
 * descriptor fields are reused as follows while the page is a slab:
 *
 *   flags        bit PG_slab marks this page as a slab page
 *   property     index of the owning kmem_cache
 *   ref          number of allocated objects in this slab
 *   page_link.next  next slab on the cache's partial list, or NULL
 *   page_link.prev  head of the in-page free object list (freelist)
 *
 * page_link.prev is not used for list linkage: the partial list is singly
 * linked through page_link.next.  This leaves one pointer-sized field for the
 * freelist head without any dynamic metadata allocation.  All cache metadata
 * lives in the static caches[] array, so metadata never asks itself for
 * memory.
 */

#define PG_slab 2 /* PG_reserved is 0, PG_property is 1 in memlayout.h */

#define SetPageSlab(page)   ((page)->flags |= (1UL << PG_slab))
#define ClearPageSlab(page) ((page)->flags &= ~(1UL << PG_slab))
#define PageSlab(page)      (((page)->flags >> PG_slab) & 1)

#define SLUB_MIN_OBJECT_SIZE sizeof(void *)
#define SLUB_MAX_OBJECT_SIZE PGSIZE

static const size_t slub_sizes[] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
};

#define NR_SLUB_CACHES (sizeof(slub_sizes) / sizeof(slub_sizes[0]))

struct kmem_cache {
    size_t size;         /* object size of this class */
    size_t objs_per_slab;/* objects in one page */
    struct Page *partial;/* slabs with at least one free object */
    size_t nr_slabs;     /* currently active slabs */
};

static struct kmem_cache caches[NR_SLUB_CACHES];
static int slub_ready = 0;

/* ------------------------------------------------------------------ */
/* Address and page helpers                                            */
/* ------------------------------------------------------------------ */

static void *slab_page_base(struct Page *page) {
    return (void *)(page2pa(page) + va_pa_offset);
}

/*
 * Map an object pointer back to its struct Page.  Return NULL for an address
 * outside the kernel-mapped physical memory.  This is the first half of kfree
 * locating the slab; the page flags then identify it as a slab page and
 * page->property identifies the cache.
 */
static struct Page *slab_page_from_ptr(const void *ptr) {
    uintptr_t va = (uintptr_t)ptr;
    uintptr_t pa;
    size_t ppn;

    if (va < KERNBASE || va >= KERNTOP) {
        return NULL;
    }
    pa = va - va_pa_offset;
    ppn = PPN(pa);
    if (ppn < nbase || ppn >= npage) {
        return NULL;
    }
    return &pages[ppn - nbase];
}

/* ------------------------------------------------------------------ */
/* Page-embedded slab metadata                                         */
/* ------------------------------------------------------------------ */

static struct Page *slab_next(struct Page *page) {
    if (page->page_link.next == NULL) {
        return NULL;
    }
    return le2page(page->page_link.next, page_link);
}

static void slab_set_next(struct Page *page, struct Page *next) {
    page->page_link.next = (next == NULL) ? NULL : &next->page_link;
}

static void *slab_freelist(struct Page *page) {
    return (void *)page->page_link.prev;
}

static void slab_set_freelist(struct Page *page, void *head) {
    page->page_link.prev = (list_entry_t *)head;
}

static void *object_next(void *obj) {
    return *(void **)obj;
}

static void object_set_next(void *obj, void *next) {
    *(void **)obj = next;
}

static void cache_add_partial(struct kmem_cache *cache, struct Page *page) {
    slab_set_next(page, cache->partial);
    cache->partial = page;
}

static int cache_remove_partial(struct kmem_cache *cache, struct Page *page) {
    struct Page *prev = NULL;
    struct Page *cur = cache->partial;

    while (cur != NULL) {
        if (cur == page) {
            if (prev == NULL) {
                cache->partial = slab_next(cur);
            } else {
                slab_set_next(prev, slab_next(cur));
            }
            slab_set_next(cur, NULL);
            return 1;
        }
        prev = cur;
        cur = slab_next(cur);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Slab operations                                                     */
/* ------------------------------------------------------------------ */

/*
 * Allocate one page from the page allocator and cut it into objects.  The
 * objects are threaded on an in-page freelist; the next pointer of a free
 * object is stored in the object itself, so no side table is needed.
 */
static struct Page *slab_create(struct kmem_cache *cache,
                                unsigned int cache_index) {
    struct Page *page = alloc_page();
    char *base;
    void *freelist = NULL;
    size_t i;

    if (page == NULL) {
        return NULL;
    }

    SetPageSlab(page);
    page->property = cache_index;
    page->ref = 0;
    slab_set_next(page, NULL);

    base = (char *)slab_page_base(page);
    for (i = cache->objs_per_slab; i > 0; i--) {
        void *obj = base + (i - 1) * cache->size;
        object_set_next(obj, freelist);
        freelist = obj;
    }
    slab_set_freelist(page, freelist);
    cache->nr_slabs++;
    return page;
}

static void *slab_alloc_object(struct kmem_cache *cache, struct Page *page) {
    void *obj = slab_freelist(page);

    assert(obj != NULL);
    assert(page->ref < cache->objs_per_slab);

    slab_set_freelist(page, object_next(obj));
    page->ref++;
    return obj;
}

/*
 * Double-free detection: every free object is reachable from the slab's
 * freelist.  If ptr is already on that list it is not an allocated object.
 * The walk is O(objects per slab); this teaching allocator favors clear
 * invariants over speed.
 */
static int slab_object_is_free(struct kmem_cache *cache, struct Page *page,
                               void *obj) {
    uintptr_t base = (uintptr_t)slab_page_base(page);
    uintptr_t end = base + PGSIZE;
    void *p = slab_freelist(page);
    size_t guard = 0;

    while (p != NULL) {
        uintptr_t addr = (uintptr_t)p;

        if (p == obj) {
            return 1;
        }
        if (addr < base || addr >= end
            || (addr - base) % cache->size != 0) {
            panic("slub: corrupted freelist in cache %lu",
                  (unsigned long)cache->size);
        }
        p = object_next(p);
        if (++guard > cache->objs_per_slab) {
            panic("slub: freelist cycle in cache %lu",
                  (unsigned long)cache->size);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public interface                                                    */
/* ------------------------------------------------------------------ */

void slub_init(void) {
    size_t i;

    if (slub_ready) {
        return;
    }

    for (i = 0; i < NR_SLUB_CACHES; i++) {
        caches[i].size = slub_sizes[i];
        caches[i].objs_per_slab = PGSIZE / slub_sizes[i];
        caches[i].partial = NULL;
        caches[i].nr_slabs = 0;
        assert(caches[i].objs_per_slab > 0);
    }

    slub_ready = 1;
    cprintf("[SLUB] %lu size classes:",
            (unsigned long)NR_SLUB_CACHES);
    for (i = 0; i < NR_SLUB_CACHES; i++) {
        cprintf(" %lu", (unsigned long)caches[i].size);
    }
    cprintf("\n");
}

void *kmalloc(size_t size) {
    struct kmem_cache *cache;
    struct Page *page;
    void *obj;
    size_t i;
    int index = -1;

    if (!slub_ready) {
        panic("kmalloc: slub_init() has not been called");
    }

    /* Explicit boundary semantics, documented in slub.h. */
    if (size == 0 || size > SLUB_MAX_OBJECT_SIZE) {
        return NULL;
    }

    for (i = 0; i < NR_SLUB_CACHES; i++) {
        if (size <= caches[i].size) {
            index = (int)i;
            break;
        }
    }
    assert(index >= 0);
    cache = &caches[index];

    /*
     * Fast path: use the partial slab first.  Only when it has no free
     * object (equivalently, the cache has no partial slab) do we ask the
     * page layer for a fresh page.
     */
    if (cache->partial != NULL) {
        page = cache->partial;
        obj = slab_alloc_object(cache, page);
        if (page->ref == cache->objs_per_slab) {
            cache->partial = slab_next(page);
            slab_set_next(page, NULL);
        }
        return obj;
    }

    page = slab_create(cache, (unsigned int)index);
    if (page == NULL) {
        return NULL;
    }
    obj = slab_alloc_object(cache, page);
    if (page->ref < cache->objs_per_slab) {
        cache_add_partial(cache, page);
    }
    return obj;
}

void kfree(void *ptr) {
    struct kmem_cache *cache;
    struct Page *page;
    uintptr_t base;
    uintptr_t offset;
    size_t inuse;
    unsigned int index;

    if (!slub_ready) {
        panic("kfree: slub_init() has not been called");
    }
    if (ptr == NULL) {
        panic("kfree: NULL is not a kmalloc object");
    }

    page = slab_page_from_ptr(ptr);
    if (page == NULL) {
        panic("kfree: pointer %p is outside managed memory", ptr);
    }
    if (!PageSlab(page)) {
        panic("kfree: pointer %p is not in a slab page", ptr);
    }

    index = page->property;
    if (index >= NR_SLUB_CACHES) {
        panic("kfree: bad cache index %u", index);
    }
    cache = &caches[index];

    base = (uintptr_t)slab_page_base(page);
    offset = (uintptr_t)ptr - base;
    if (offset >= PGSIZE || offset % cache->size != 0) {
        panic("kfree: pointer %p is not an object boundary (size %lu)",
              ptr, (unsigned long)cache->size);
    }
    if (page->ref == 0 || page->ref > cache->objs_per_slab) {
        panic("kfree: corrupted slab refcount %d", page->ref);
    }
    if (slab_object_is_free(cache, page, ptr)) {
        panic("kfree: double free of %p", ptr);
    }

    inuse = page->ref;

    /*
     * The slab has no live object left: unlink it from the partial list and
     * return the whole page to the first-layer allocator.  After all objects
     * are freed, every slab created by kmalloc is gone, so the free-page
     * count returns exactly to its pre-allocation value.
     */
    if (inuse == 1) {
        if (inuse != cache->objs_per_slab
            && !cache_remove_partial(cache, page)) {
            panic("kfree: slab missing from partial list");
        }

        ClearPageSlab(page);
        page->property = 0;
        page->ref = 0;
        slab_set_next(page, NULL);
        slab_set_freelist(page, NULL);
        assert(cache->nr_slabs > 0);
        cache->nr_slabs--;
        free_page(page);
        return;
    }

    object_set_next(ptr, slab_freelist(page));
    slab_set_freelist(page, ptr);
    page->ref = inuse - 1;

    /* A full slab was not on any list; one free object makes it partial. */
    if (inuse == cache->objs_per_slab) {
        cache_add_partial(cache, page);
    }
}
