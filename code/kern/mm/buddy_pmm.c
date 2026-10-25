#include <pmm.h>
#include <list.h>
#include <stdio.h>
#include <buddy_pmm.h>

//lab2 function2-3: buddy system physical memory manager

// A buddy allocator keeps one free list per order.  An order-k block holds
// 2^k pages and its head page number is 2^k aligned, so the partner ("buddy")
// of a block is obtained by flipping the k-th bit of the head page number.
// 128MB of physical memory is at most 2^15 pages, so 16 orders (0..15) are
// enough to cover every block that can appear.
#define MAX_ORDER 16

static free_area_t free_area[MAX_ORDER];
static size_t nr_free;

static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
static void buddy_check(void);

// Insert a free block into the list of its order, keeping the list sorted by
// page address in ascending order.
static void
buddy_insert_block(struct Page *base, size_t order) {
    size_t size = 1UL << order;
    list_entry_t *head = &free_area[order].free_list;
    list_entry_t *le = head;

    base->property = size;
    SetPageProperty(base);
    while ((le = list_next(le)) != head) {
        struct Page *p = le2page(le, page_link);
        if (base < p) {
            break;
        }
    }
    // le is either the first block above base or the list head; in both
    // cases inserting before le keeps the ascending address order.
    list_add_before(le, &(base->page_link));
    free_area[order].nr_free += size;
    nr_free += size;
}

// Unlink a free block from the list of its order.  The total free page count
// is decreased here as well, and the old block head stops being free.
static void
buddy_remove_block(struct Page *base, size_t order) {
    size_t size = 1UL << order;
    list_del(&(base->page_link));
    ClearPageProperty(base);
    free_area[order].nr_free -= size;
    nr_free -= size;
}

// Return the buddy page of an order-k block, or NULL when the buddy would
// fall outside the managed page array.
static struct Page *
buddy_get_buddy(struct Page *base, size_t order) {
    size_t size = 1UL << order;
    size_t ppn = page2ppn(base) ^ size;
    if (ppn < nbase || ppn + size > npage) {
        return NULL;
    }
    return pages + (ppn - nbase);
}

static void
buddy_init(void) {
    for (size_t order = 0; order < MAX_ORDER; order++) {
        list_init(&(free_area[order].free_list));
        free_area[order].nr_free = 0;
    }
    nr_free = 0;
}

// The initial free memory does not need to start at a power-of-two aligned
// page, so it is cut greedily from its start: at every step take the largest
// 2^k block that is both 2^k aligned and not longer than the rest.  Each piece
// is put into the free list of its own order.
static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }

    p = base;
    size_t remaining = n;
    while (remaining > 0) {
        size_t order = 0;
        while (order + 1 < MAX_ORDER
               && (1UL << (order + 1)) <= remaining
               && (page2ppn(p) & ((1UL << (order + 1)) - 1)) == 0) {
            order++;
        }
        size_t size = 1UL << order;
        buddy_insert_block(p, order);
        p += size;
        remaining -= size;
    }
}

// Allocate the smallest order-k block with 2^k >= n.  If that order is empty,
// climb to higher orders; every split keeps the low half for the caller (the
// low half is given out) and returns the high half to the lower order list.
static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);

    size_t order = 0;
    while (order < MAX_ORDER && (1UL << order) < n) {
        order++;
    }
    if (order >= MAX_ORDER) {
        return NULL;
    }

    size_t cur = order;
    while (cur < MAX_ORDER && list_empty(&(free_area[cur].free_list))) {
        cur++;
    }
    if (cur == MAX_ORDER) {
        return NULL;
    }

    struct Page *page = le2page(list_next(&(free_area[cur].free_list)), page_link);
    buddy_remove_block(page, cur);

    while (cur > order) {
        cur--;
        buddy_insert_block(page + (1UL << cur), cur);
    }
    page->property = 1UL << order;
    return page;
}

// Free one aligned power-of-two block: link it first, then merge with the
// buddy level by level while the partner is a free block of the same order.
static void
buddy_free_block(struct Page *base, size_t order) {
    size_t size = 1UL << order;
    struct Page *buddy;

    buddy_insert_block(base, order);
    while (order + 1 < MAX_ORDER) {
        buddy = buddy_get_buddy(base, order);
        if (buddy == NULL || !PageProperty(buddy) || buddy->property != size) {
            break;
        }
        buddy_remove_block(buddy, order);
        buddy_remove_block(base, order);
        if (buddy < base) {
            base = buddy;
        }
        order++;
        size <<= 1;
        buddy_insert_block(base, order);
    }
}

// A caller may free a range of any length, so the range is cut into aligned
// power-of-two pieces exactly like init_memmap does and each piece is freed on
// its own.
static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }

    p = base;
    size_t remaining = n;
    while (remaining > 0) {
        size_t order = 0;
        while (order + 1 < MAX_ORDER
               && (1UL << (order + 1)) <= remaining
               && (page2ppn(p) & ((1UL << (order + 1)) - 1)) == 0) {
            order++;
        }
        size_t size = 1UL << order;
        buddy_free_block(p, order);
        p += size;
        remaining -= size;
    }
}

static size_t
buddy_nr_free_pages(void) {
    return nr_free;
}

// Check that every order list is ordered by address, that every head really
// describes a 2^k aligned block of 2^k pages, and that the per-order and total
// free page counters agree with the lists.
static void
buddy_check_lists(void) {
    size_t total = 0;
    for (size_t order = 0; order < MAX_ORDER; order++) {
        size_t size = 1UL << order;
        size_t order_free = 0;
        struct Page *prev = NULL;
        list_entry_t *head = &(free_area[order].free_list);
        list_entry_t *le = head;
        while ((le = list_next(le)) != head) {
            struct Page *p = le2page(le, page_link);
            assert(PageProperty(p));
            assert(p->property == size);
            assert((page2ppn(p) & (size - 1)) == 0);
            assert(page2ppn(p) >= nbase && page2ppn(p) + size <= npage);
            if (prev != NULL) {
                assert(prev < p);
            }
            prev = p;
            order_free += size;
        }
        assert(order_free == free_area[order].nr_free);
        total += order_free;
    }
    assert(total == nr_free_pages());
}

// Return the free block whose range contains target (used by buddy_check).
static struct Page *
buddy_find_covering(struct Page *target) {
    for (size_t order = 0; order < MAX_ORDER; order++) {
        list_entry_t *head = &(free_area[order].free_list);
        list_entry_t *le = head;
        while ((le = list_next(le)) != head) {
            struct Page *p = le2page(le, page_link);
            if (p <= target && target < p + p->property) {
                return p;
            }
        }
    }
    return NULL;
}

// ============================ buddy tests ============================
// buddy_check only observes the allocator state (free_area[] and the total
// free page count) and drives it through alloc_pages()/free_pages(); none of
// the allocation functions above are changed by the tests.

#define TEST_MAX_BLOCKS 64

static struct Page *test_blocks[TEST_MAX_BLOCKS];
static size_t test_orders[TEST_MAX_BLOCKS];

// Count the blocks and the pages held by one order list.
static void
buddy_order_stat(size_t order, size_t *blocks, size_t *pages) {
    size_t nb = 0;
    size_t np = 0;
    list_entry_t *head = &(free_area[order].free_list);
    list_entry_t *le = head;
    while ((le = list_next(le)) != head) {
        struct Page *p = le2page(le, page_link);
        nb++;
        np += p->property;
    }
    *blocks = nb;
    *pages = np;
}

static size_t
buddy_block_count(size_t order) {
    size_t blocks, pages;
    buddy_order_stat(order, &blocks, &pages);
    return blocks;
}

// Remember the free block distribution of every order.
static void
buddy_take_snapshot(size_t *blocks, size_t *pages) {
    for (size_t order = 0; order < MAX_ORDER; order++) {
        buddy_order_stat(order, &blocks[order], &pages[order]);
    }
}

// Check the free lists against a snapshot: same block count and same page
// count for every order, and the same total amount of free memory.
static void
buddy_check_snapshot(const size_t *blocks, const size_t *pages,
                     size_t total_free) {
    buddy_check_lists();
    for (size_t order = 0; order < MAX_ORDER; order++) {
        size_t nb, np;
        buddy_order_stat(order, &nb, &np);
        assert(nb == blocks[order]);
        assert(np == pages[order]);
        assert(np == free_area[order].nr_free);
        assert(nb * (1UL << order) == np);
    }
    assert(nr_free_pages() == total_free);
}

// Framework entry.  It runs in four stages:
//   1. validate the initial free lists order by order and snapshot them;
//   2. allocate and free one block of every reachable order, restoring the
//      snapshot each time;
//   3. Case 1: non power-of-two requests round up, and the allocator reports
//      failure once every free page has been handed out;
//   4. Case 2: with no free page left, aligned contiguous pages coalesce and
//      non-buddy pages do not merge;
//   5. Case 3: free every page one by one and compare with the snapshot.
static void
buddy_check(void) {
    size_t init_blocks[MAX_ORDER];
    size_t init_pages[MAX_ORDER];
    size_t init_total;

    // (1) 逐阶校验：地址升序、按阶对齐、各阶块页数一致，页数之和等于
    //     总空闲页数。
    buddy_check_lists();
    buddy_take_snapshot(init_blocks, init_pages);
    init_total = nr_free_pages();
    {
        size_t sum = 0;
        for (size_t order = 0; order < MAX_ORDER; order++) {
            assert(init_pages[order] == free_area[order].nr_free);
            sum += init_pages[order];
        }
        assert(sum == init_total);
    }

    // (2) 各阶分配/释放往返：请求 2^k 页再原样归还，块数与总数必须复原。
    for (size_t order = 0; order < MAX_ORDER; order++) {
        size_t size = 1UL << order;
        size_t before = nr_free_pages();
        struct Page *p = alloc_pages(size);
        if (p == NULL) {
            continue;
        }
        assert(!PageProperty(p));
        assert((page2ppn(p) & (size - 1)) == 0);
        assert(nr_free_pages() == before - size);
        free_pages(p, size);
        buddy_check_snapshot(init_blocks, init_pages, init_total);
    }

    // (3a) Case 1：非 2 的幂请求向上取整，3 页占 4 页，5 页占 8 页。
    {
        size_t before = nr_free_pages();
        struct Page *p = alloc_pages(3);
        assert(p != NULL && !PageProperty(p));
        assert(nr_free_pages() == before - 4);
        assert((page2ppn(p) & 3) == 0);
        free_pages(p, 4);
        assert(nr_free_pages() == before);

        p = alloc_pages(5);
        assert(p != NULL && !PageProperty(p));
        assert(nr_free_pages() == before - 8);
        assert((page2ppn(p) & 7) == 0);
        free_pages(p, 8);
        buddy_check_snapshot(init_blocks, init_pages, init_total);
    }

    // (3b) Case 1：从高阶到低阶取走全部空闲块，直到分配耗尽。
    size_t nblocks = 0;
    for (size_t order = MAX_ORDER; order-- > 0; ) {
        while (free_area[order].nr_free > 0) {
            struct Page *p = alloc_pages(1UL << order);
            assert(p != NULL);
            assert(!PageProperty(p));
            assert((page2ppn(p) & ((1UL << order) - 1)) == 0);
            assert(nblocks < TEST_MAX_BLOCKS);
            test_blocks[nblocks] = p;
            test_orders[nblocks] = order;
            nblocks++;
        }
    }
    assert(nr_free_pages() == 0);
    buddy_check_lists();
    assert(alloc_page() == NULL);
    assert(alloc_pages(2) == NULL);
    assert(alloc_pages(3) == NULL);
    cprintf("[Buddy] Case 1: PASS\n");

    // (4) Case 2：现在没有空闲页。归还一段 2 阶对齐的连续 4 页，必须合成
    //     一个 2 阶块；随后归还两个互不为伙伴的页，二者不得合并。
    int pick = -1;
    for (size_t i = 0; i < nblocks; i++) {
        if (test_orders[i] >= 3
            && (pick < 0 || test_orders[i] < test_orders[pick])) {
            pick = (int)i;
        }
    }
    assert(pick >= 0);
    struct Page *b = test_blocks[pick];
    size_t border = test_orders[pick];
    assert((page2ppn(b) & ((1UL << border) - 1)) == 0);

    free_pages(b, 4);
    assert(nr_free_pages() == 4);
    assert(free_area[2].nr_free == 4);
    assert(buddy_block_count(2) == 1);
    assert(free_area[0].nr_free == 0 && free_area[1].nr_free == 0);
    assert(le2page(list_next(&(free_area[2].free_list)), page_link) == b);
    assert(b->property == 4);
    buddy_check_lists();

    // b+4 与 b+6 不是伙伴（它们的伙伴分别是 b+5 和 b+7），不得合并。
    free_page(b + 4);
    assert(nr_free_pages() == 5);
    assert(free_area[0].nr_free == 1);
    free_page(b + 6);
    assert(nr_free_pages() == 6);
    assert(free_area[0].nr_free == 2);
    assert(buddy_block_count(0) == 2);
    assert(free_area[1].nr_free == 0);
    {
        list_entry_t *le = list_next(&(free_area[0].free_list));
        assert(le2page(le, page_link) == b + 4);
        le = list_next(le);
        assert(le2page(le, page_link) == b + 6);
    }
    buddy_check_lists();

    // 补齐 b 的前 8 页后，所有块应逐级合并回一个 3 阶块。
    free_page(b + 5);
    free_page(b + 7);
    assert(nr_free_pages() == 8);
    assert(free_area[3].nr_free == 8);
    assert(buddy_block_count(3) == 1);
    assert(free_area[0].nr_free == 0
           && free_area[1].nr_free == 0
           && free_area[2].nr_free == 0);
    assert(buddy_find_covering(b) == b);
    assert(b->property == 8);
    buddy_check_lists();
    cprintf("[Buddy] Case 2: PASS\n");

    // b 所在块的其余页也逐页归还，之后不再交给 Case 3。
    for (size_t i = 8; i < (1UL << border); i++) {
        free_page(b + i);
    }
    test_blocks[pick] = NULL;
    buddy_check_lists();

    // (5) Case 3：其余页逐页全部归还，总数与各阶块数回到初始快照。
    for (size_t i = 0; i < nblocks; i++) {
        struct Page *blk = test_blocks[i];
        if (blk == NULL) {
            continue;
        }
        size_t size = 1UL << test_orders[i];
        for (size_t j = 0; j < size; j++) {
            free_page(blk + j);
        }
        test_blocks[i] = NULL;
    }
    buddy_check_snapshot(init_blocks, init_pages, init_total);
    cprintf("[Buddy] Case 3: PASS\n");
    cprintf("[Buddy] all tests passed, free pages = %lu\n",
            (unsigned long)nr_free_pages());
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
