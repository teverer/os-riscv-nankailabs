#include <assert.h>
#include <defs.h>
#include <mmu.h>
#include <pmm.h>
#include <slub.h>
#include <stdio.h>

/*
 * Black-box tests for the SLUB object allocator.
 *
 * Only kmalloc()/kfree() and the page allocator query nr_free_pages() are
 * used.  No field of struct Page, no kmem_cache and no internal list/freelist
 * is inspected.
 *
 * Interface/error semantics taken from slub.h:
 *   kmalloc(0)              -> NULL
 *   kmalloc(size > PGSIZE)  -> NULL
 *   returned object         -> aligned to its size class
 *   kfree(NULL)             -> panic
 *   invalid/double free     -> panic
 * The implementation keeps no empty slab, so every completed test must leave
 * nr_free_pages() exactly equal to its value at the start of that test.
 *
 * check_slub() runs checks 1-6 and 8 automatically and prints a summary.
 * Check 7 deliberately makes the kernel panic, so it cannot return to a
 * summary.  It is provided as a separate minimal stage:
 *
 *      check_slub_bad_use(0);   double free
 *      check_slub_bad_use(1);   kfree() of a stack address
 *      check_slub_bad_use(2);   kfree(NULL)
 *      check_slub_bad_use(3);   misaligned pointer inside a slab object
 *
 * Each call must print the allocator's panic and never return.  If it does
 * return, the test prints its own failure panic.
 */

#define CHECK_OBJECTS 32
#define GROW_OBJECTS 640
#define SHRINK_OBJECTS 200
#define INTERLEAVED_OBJECTS 256
#define RANDOM_LIVE 384
#define RANDOM_ITERATIONS 4000

/* The size classes documented in slub.h. */
static const size_t check_classes[] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
};

#define NR_CHECK_CLASSES (sizeof(check_classes) / sizeof(check_classes[0]))

static size_t class_size(size_t request) {
    size_t i;

    if (request == 0 || request > PGSIZE) {
        return 0;
    }
    for (i = 0; i < NR_CHECK_CLASSES; i++) {
        if (request <= check_classes[i]) {
            return check_classes[i];
        }
    }
    return 0;
}

static unsigned char pattern_byte(uint32_t tag, size_t offset) {
    return (unsigned char)((tag * 131U + offset * 37U
                            + (offset >> 8) * 13U + (offset >> 16) * 7U)
                           & 0xffU);
}

static void fill_pattern(void *ptr, size_t len, uint32_t tag) {
    unsigned char *p = (unsigned char *)ptr;
    size_t i;

    for (i = 0; i < len; i++) {
        p[i] = pattern_byte(tag, i);
    }
}

static void check_pattern(const void *ptr, size_t len, uint32_t tag,
                          const char *where) {
    const unsigned char *p = (const unsigned char *)ptr;
    size_t i;

    for (i = 0; i < len; i++) {
        if (p[i] != pattern_byte(tag, i)) {
            panic("check_slub: %s: pattern mismatch at byte %lu",
                  where, (unsigned long)i);
        }
    }
}

static void expect_pages(size_t expected, const char *where) {
    size_t now = nr_free_pages();

    if (now != expected) {
        panic("check_slub: %s: free pages %lu, expected %lu",
              where, (unsigned long)now, (unsigned long)expected);
    }
}

/* ------------------------------------------------------------------ */
/* 1. Allocation validity and declared boundary semantics             */
/*    Catches: freelist corruption, wrong size-class rounding, objects  */
/*    smaller than the requested size, and boundary-policy regressions. */
/* ------------------------------------------------------------------ */
static void test_alloc_validity(void) {
    static const size_t requests[] = {
        1, 4, 8, 17, 64, 100, 256, 1000, 2048, 3000, 4095, 4096,
    };
    size_t before = nr_free_pages();
    size_t i;

    for (i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        size_t len = class_size(requests[i]);
        void *p = kmalloc(requests[i]);
        uint32_t tag = (uint32_t)(0x1000 + i);

        if (p == NULL) {
            panic("check_slub: kmalloc(%lu) returned NULL",
                  (unsigned long)requests[i]);
        }
        fill_pattern(p, len, tag);
        check_pattern(p, len, tag, "alloc validity");
        kfree(p);
        expect_pages(before, "alloc validity");
    }

    /* Boundary semantics declared by slub.h. */
    assert(kmalloc(0) == NULL);
    assert(kmalloc(PGSIZE + 1) == NULL);
    assert(kmalloc((size_t)-1) == NULL);
    expect_pages(before, "alloc boundary");

    cprintf("[SLUB Check] 1. allocation validity & boundary: PASS\n");
}

/* ------------------------------------------------------------------ */
/* 2. Non-overlap and alignment                                       */
/*    Catches: different caches returning overlapping storage, broken  */
/*    object partitioning inside a slab, and alignment regressions.     */
/* ------------------------------------------------------------------ */
static void test_nonoverlap_alignment(void) {
    static const size_t requests[] = {
        1, 9, 33, 100, 257, 1000, 3000, 4096,
    };
    void *objects[CHECK_OBJECTS];
    size_t lengths[CHECK_OBJECTS];
    uint32_t tags[CHECK_OBJECTS];
    size_t before = nr_free_pages();
    size_t i;

    for (i = 0; i < CHECK_OBJECTS; i++) {
        size_t req = requests[i % (sizeof(requests) / sizeof(requests[0]))];

        objects[i] = kmalloc(req);
        if (objects[i] == NULL) {
            panic("check_slub: non-overlap kmalloc(%lu) failed",
                  (unsigned long)req);
        }
        lengths[i] = class_size(req);
        tags[i] = (uint32_t)(0x2000 + i);

        if (((uintptr_t)objects[i] % lengths[i]) != 0) {
            panic("check_slub: object %p not aligned to %lu",
                  objects[i], (unsigned long)lengths[i]);
        }
        fill_pattern(objects[i], lengths[i], tags[i]);
    }

    /* Every object is written only after all of them were allocated, so any
     * overlap between different size classes is visible here. */
    for (i = 0; i < CHECK_OBJECTS; i++) {
        check_pattern(objects[i], lengths[i], tags[i], "non-overlap");
    }

    for (i = 0; i < CHECK_OBJECTS; i++) {
        kfree(objects[i]);
    }
    expect_pages(before, "non-overlap");

    cprintf("[SLUB Check] 2. non-overlap & alignment: PASS\n");
}

/* ------------------------------------------------------------------ */
/* 3. Freed object reuse                                              */
/*    Catches: freed objects not returned to the freelist, wrong       */
/*    freelist LIFO behavior, and premature page return while a slab    */
/*    still has live objects.                                          */
/* ------------------------------------------------------------------ */
static void test_free_reuse(void) {
    size_t before = nr_free_pages();
    void *a = kmalloc(64);
    void *b = kmalloc(64);
    void *c;

    if (a == NULL || b == NULL) {
        panic("check_slub: reuse setup allocation failed");
    }
    fill_pattern(a, 64, 0x3001);
    fill_pattern(b, 64, 0x3002);
    check_pattern(a, 64, 0x3001, "reuse setup A");
    check_pattern(b, 64, 0x3002, "reuse setup B");

    kfree(a);
    c = kmalloc(64);
    if (c == NULL) {
        panic("check_slub: reuse allocation failed");
    }
    if (c != a) {
        panic("check_slub: freed object %p was not reused, got %p", a, c);
    }

    fill_pattern(c, 64, 0x3003);
    check_pattern(c, 64, 0x3003, "reuse new object");
    check_pattern(b, 64, 0x3002, "reuse survivor");

    kfree(c);
    kfree(b);
    expect_pages(before, "reuse");

    cprintf("[SLUB Check] 3. freed object reuse: PASS\n");
}

/* ------------------------------------------------------------------ */
/* 4. Slab growth far beyond one page's object capacity               */
/*    Catches: failure to request new pages when the partial slab is   */
/*    full, partial-list corruption, and allocations overlapping live   */
/*    objects during growth.                                           */
/* ------------------------------------------------------------------ */
static void test_slab_growth(void) {
    static void *objects[GROW_OBJECTS];
    static uint32_t tags[GROW_OBJECTS];
    size_t before = nr_free_pages();
    size_t i;
    size_t holes = 0;

    /* Class 32 holds 128 objects/page, so 640 objects need several slabs. */
    for (i = 0; i < GROW_OBJECTS; i++) {
        objects[i] = kmalloc(24);
        if (objects[i] == NULL) {
            panic("check_slub: growth allocation %lu failed", (unsigned long)i);
        }
        tags[i] = (uint32_t)(0x4000 + i);
        fill_pattern(objects[i], 32, tags[i]);

        /* Intersperse a few frees while the cache is still growing. */
        if (i > 0 && (i % 137) == 136) {
            size_t victim = i / 2;
            if (objects[victim] != NULL) {
                check_pattern(objects[victim], 32, tags[victim],
                              "growth pre-free");
                kfree(objects[victim]);
                objects[victim] = NULL;
                holes++;
            }
        }
    }

    /* Refill the holes: freed slots may be reused, but survivors must stay
     * intact and no new allocation may overlap a survivor. */
    for (i = 0; i < GROW_OBJECTS && holes > 0; i++) {
        if (objects[i] == NULL) {
            objects[i] = kmalloc(24);
            if (objects[i] == NULL) {
                panic("check_slub: growth refill failed");
            }
            tags[i] = (uint32_t)(0x5000 + i);
            fill_pattern(objects[i], 32, tags[i]);
            holes--;
        }
    }
    assert(holes == 0);

    for (i = 0; i < GROW_OBJECTS; i++) {
        check_pattern(objects[i], 32, tags[i], "growth final");
    }
    for (i = 0; i < GROW_OBJECTS; i++) {
        kfree(objects[i]);
    }
    expect_pages(before, "growth");

    cprintf("[SLUB Check] 4. slab growth (%lu objects): PASS\n",
            (unsigned long)GROW_OBJECTS);
}

/* ------------------------------------------------------------------ */
/* 5. Slab shrink: exact return to the initial page count             */
/*    Catches: empty slabs retained in the cache, lost free_pages(),   */
/*    and leaks in the empty-slab removal path.                        */
/* ------------------------------------------------------------------ */
static void test_slab_shrink(void) {
    static const size_t requests[] = {
        8, 17, 64, 300, 512, 1000, 2048, 4096,
    };
    static void *objects[SHRINK_OBJECTS];
    static size_t lengths[SHRINK_OBJECTS];
    size_t before = nr_free_pages();
    size_t i;

    for (i = 0; i < SHRINK_OBJECTS; i++) {
        size_t req = requests[i % (sizeof(requests) / sizeof(requests[0]))];

        objects[i] = kmalloc(req);
        if (objects[i] == NULL) {
            panic("check_slub: shrink allocation %lu failed",
                  (unsigned long)i);
        }
        lengths[i] = class_size(req);
        fill_pattern(objects[i], lengths[i], (uint32_t)(0x6000 + i));
    }
    for (i = 0; i < SHRINK_OBJECTS; i++) {
        check_pattern(objects[i], lengths[i], (uint32_t)(0x6000 + i),
                      "shrink");
        kfree(objects[i]);
    }

    /* slub.h declares no retained per-cache empty slab. */
    expect_pages(before, "shrink");

    cprintf("[SLUB Check] 5. slab shrink & no leak: PASS\n");
}

/* ------------------------------------------------------------------ */
/* 6. Interleaved allocation/free, survivor integrity                 */
/*    Catches: reusing memory of a live object, stale freelist entries, */
/*    and partial-list bookkeeping errors after mixed frees/allocations.*/
/* ------------------------------------------------------------------ */
static void test_interleaved(void) {
    static void *objects[INTERLEAVED_OBJECTS];
    static uint32_t tags[INTERLEAVED_OBJECTS];
    size_t before = nr_free_pages();
    size_t i;

    for (i = 0; i < INTERLEAVED_OBJECTS; i++) {
        objects[i] = kmalloc(48);
        if (objects[i] == NULL) {
            panic("check_slub: interleaved allocation %lu failed",
                  (unsigned long)i);
        }
        tags[i] = (uint32_t)(0x7000 + i);
        fill_pattern(objects[i], 64, tags[i]);
    }

    for (i = 0; i < INTERLEAVED_OBJECTS; i += 3) {
        check_pattern(objects[i], 64, tags[i], "interleaved pre-free");
        kfree(objects[i]);
        objects[i] = NULL;
    }
    for (i = 1; i < INTERLEAVED_OBJECTS; i++) {
        if (objects[i] != NULL) {
            check_pattern(objects[i], 64, tags[i], "interleaved survivor");
        }
    }

    for (i = 0; i < INTERLEAVED_OBJECTS; i += 3) {
        objects[i] = kmalloc(48);
        if (objects[i] == NULL) {
            panic("check_slub: interleaved refill failed");
        }
        tags[i] = (uint32_t)(0x8000 + i);
        fill_pattern(objects[i], 64, tags[i]);
    }

    /* Re-check survivors after refill: new allocations must not have reused
     * memory that still belongs to a live object. */
    for (i = 0; i < INTERLEAVED_OBJECTS; i++) {
        check_pattern(objects[i], 64, tags[i], "interleaved final");
    }
    for (i = 0; i < INTERLEAVED_OBJECTS; i++) {
        kfree(objects[i]);
    }
    expect_pages(before, "interleaved");

    cprintf("[SLUB Check] 6. interleaved alloc/free: PASS\n");
}

/* ------------------------------------------------------------------ */
/* 8. Fixed-seed random stress                                        */
/*    Catches: cache-mapping mistakes, size-class mixups, freelist      */
/*    corruption, and leak accumulation under mixed allocation/free.    */
/* ------------------------------------------------------------------ */
struct random_object {
    void *ptr;
    size_t length;
    uint32_t tag;
};

static struct random_object random_objects[RANDOM_LIVE];
static uint32_t random_state = 0x12345678U;

static uint32_t random_next(void) {
    random_state = random_state * 1664525U + 1013904223U;
    return random_state;
}

static void test_random_stress(void) {
    static const size_t requests[] = {
        1, 2, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 200, 255,
        256, 500, 511, 512, 1000, 1023, 1024, 2000, 2047, 2048, 3000,
        4095, 4096,
    };
    size_t before = nr_free_pages();
    size_t live = 0;
    size_t iteration;

    random_state = 0x12345678U;

    for (iteration = 0; iteration < RANDOM_ITERATIONS; iteration++) {
        uint32_t r = random_next();
        int do_alloc = (live == 0)
                       || (live < RANDOM_LIVE && (r % 100U) < 55U);

        if (do_alloc) {
            size_t k = (size_t)((r >> 8) % (sizeof(requests) / sizeof(requests[0])));
            size_t req = requests[k];
            size_t len = class_size(req);
            void *p = kmalloc(req);

            if (p == NULL) {
                panic("check_slub: random kmalloc(%lu) failed at iteration %lu",
                      (unsigned long)req, (unsigned long)iteration);
            }
            random_objects[live].ptr = p;
            random_objects[live].length = len;
            random_objects[live].tag = (uint32_t)(0x900000U + iteration);
            fill_pattern(p, len, random_objects[live].tag);
            live++;
        } else {
            size_t victim = (size_t)((r >> 8) % live);

            check_pattern(random_objects[victim].ptr,
                          random_objects[victim].length,
                          random_objects[victim].tag, "random victim");
            kfree(random_objects[victim].ptr);
            live--;
            random_objects[victim] = random_objects[live];
        }

        if ((iteration & 0x3fU) == 0 && live > 0) {
            size_t sample = (size_t)(random_next() % live);
            check_pattern(random_objects[sample].ptr,
                          random_objects[sample].length,
                          random_objects[sample].tag, "random sample");
        }
    }

    for (iteration = 0; iteration < live; iteration++) {
        check_pattern(random_objects[iteration].ptr,
                      random_objects[iteration].length,
                      random_objects[iteration].tag, "random final");
    }
    for (iteration = 0; iteration < live; iteration++) {
        kfree(random_objects[iteration].ptr);
    }
    expect_pages(before, "random");

    cprintf("[SLUB Check] 8. fixed-seed random stress: PASS\n");
}

/* ------------------------------------------------------------------ */
/* Unified entry                                                      */
/* ------------------------------------------------------------------ */
void check_slub(void) {
    size_t before = nr_free_pages();

    cprintf("[SLUB Check] start, free pages = %lu\n",
            (unsigned long)before);

    test_alloc_validity();
    test_nonoverlap_alignment();
    test_free_reuse();
    test_slab_growth();
    test_slab_shrink();
    test_interleaved();
    test_random_stress();

    expect_pages(before, "final");
    cprintf("[SLUB Check] check 7 is a panic stage: call "
            "check_slub_bad_use(case) separately (panic is expected)\n");
    cprintf("[SLUB Check] SUMMARY: functional checks 1-6 and 8 PASS, "
            "free pages restored to %lu\n", (unsigned long)before);
}

/*
 * Check 7 cannot be part of check_slub(): the allocator must panic, and
 * __panic() never returns.  Run one case per boot.  Every case must show a
 * panic from slub.c before reaching the fail-safe panic below.
 */
void check_slub_bad_use(int which) {
    void *a;
    void *b;
    volatile int stack_object = 0;

    cprintf("[SLUB Check] 7. illegal use case %d: a kernel panic is EXPECTED\n",
            which);

    switch (which) {
    case 0: /* double free while the slab still has a live object */
        a = kmalloc(64);
        b = kmalloc(64);
        if (a == NULL || b == NULL) {
            panic("check_slub: illegal-use setup failed");
        }
        (void)b;
        kfree(a);
        kfree(a);
        break;
    case 1: /* pointer into the kernel stack */
        kfree((void *)&stack_object);
        break;
    case 2: /* NULL */
        kfree(NULL);
        break;
    case 3: /* pointer that is not at an object boundary */
        a = kmalloc(64);
        if (a == NULL) {
            panic("check_slub: illegal-use setup failed");
        }
        kfree((void *)((char *)a + 1));
        break;
    default:
        panic("check_slub: bad-use case must be 0..3");
    }

    /* Reached only if kfree() failed to detect an invalid operation. */
    panic("check_slub: illegal kfree was NOT detected (case %d)", which);
}
