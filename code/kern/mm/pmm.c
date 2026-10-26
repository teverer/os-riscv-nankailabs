#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);
static void page_init(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &buddy_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// 模块二任务二（Best-Fit）阶段的 init_pmm_manager()
// static void init_pmm_manager(void) {
//     pmm_manager = &best_fit_pmm_manager;
//     cprintf("memory management: %s\n", pmm_manager->name);
//     pmm_manager->init();
// }

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2411353
static void page_init(void) {
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    if (mem_base == 0 || mem_size == 0) {
        panic("page_init: invalid physical memory information from DTB");
    }

    uint64_t mem_end = mem_base + mem_size;

    // The kernel is linked at KERNBASE and mapped with this fixed offset.
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // Upper bound (exclusive) of the manageable physical page numbers.
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: physical memory ends below DRAM base");
    }

    // The page descriptor array starts right after the kernel image and is
    // page aligned. Its virtual address is in the kernel mapping.
    extern char end[];
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);

    // Physical range occupied by the descriptor array: one descriptor for
    // every page in [nbase, npage).
    uintptr_t pages_pa = PADDR((uintptr_t)pages);
    uintptr_t pages_end_pa =
        pages_pa + (npage - nbase) * sizeof(struct Page);

    // Reserve every page first, so that the kernel image, the descriptor
    // array and the unaligned tail can never be handed to the allocator.
    struct Page *p;
    for (p = pages; p != pages + (npage - nbase); p++) {
        p->flags = 0;
        SetPageReserved(p);
    }

    cprintf("Physical memory: base = 0x%016lx, size = 0x%016lx\n",
            mem_base, mem_size);
    cprintf("  pages = %p, npage = 0x%lx, nbase = 0x%lx\n",
            pages, (unsigned long)npage, (unsigned long)nbase);

    // Free the complete pages after the descriptor array. Both bounds are
    // page aligned: the start rounds up, the DTB-reported end rounds down.
    uintptr_t free_start = ROUNDUP(pages_end_pa, PGSIZE);
    uintptr_t free_end = ROUNDDOWN(mem_end, PGSIZE);

    // Only call init_memmap() when there is at least one complete free page.
    if (free_start < free_end) {
        init_memmap(pa2page(free_start), (free_end - free_start) / PGSIZE);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
