#include "paging.h"
#include "pmm.h"
#include "vga.h"
#include "string.h"

// ============================================================
// State
// ============================================================

// page directory - 1024 entries, each covering 4MB
static pde_t page_directory[1024] __attribute__((aligned(4096)));

// identity map the first 4MB for the kernel
static pte_t kernel_page_table[1024] __attribute__((aligned(4096)));

// extra page tables for allocations beyond 4MB
// we keep these in the identity mapped region by allocating them statically
#define EXTRA_PAGE_TABLES 8
static pte_t extra_page_tables[EXTRA_PAGE_TABLES][1024] __attribute__((aligned(4096)));
static int   extra_page_table_used[EXTRA_PAGE_TABLES] = {0};

extern uint32_t kernel_end;

// ============================================================
// Internal helpers
// ============================================================

static void flush_tlb(uint32_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static pte_t *alloc_page_table() {
    int i;
    for (i = 0; i < EXTRA_PAGE_TABLES; i++) {
        if (!extra_page_table_used[i]) {
            extra_page_table_used[i] = 1;
            memset(extra_page_tables[i], 0, PAGE_SIZE);
            return extra_page_tables[i];
        }
    }
    vga_printf("Paging: out of static page tables!\n");
    return 0;
}

static pte_t *get_page_table(uint32_t virt, int create, uint32_t flags) {
    uint32_t pdi = virt >> 22;

    if (!(page_directory[pdi] & PAGE_PRESENT)) {
        if (!create) return 0;

        pte_t *pt = alloc_page_table();
        if (!pt) return 0;

        page_directory[pdi] = (uint32_t)pt | PAGE_PRESENT | PAGE_RW | flags;
    }

    return (pte_t *)(page_directory[pdi] & ~0xFFF);
}

// ============================================================
// Public API
// ============================================================

void paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    pte_t *pt = get_page_table(virt, 1, flags);
    if (!pt) {
        vga_printf("Paging: failed to get page table for 0x%x\n", virt);
        return;
    }
    uint32_t pti = (virt >> 12) & 0x3FF;
    pt[pti] = (phys & ~0xFFF) | flags | PAGE_PRESENT;
    flush_tlb(virt);
}

void paging_unmap(uint32_t virt) {
    pte_t *pt = get_page_table(virt, 0, 0);
    if (!pt) return;
    uint32_t pti = (virt >> 12) & 0x3FF;
    pt[pti] = 0;
    flush_tlb(virt);
}

void paging_unmap_range(uint32_t virt_start, uint32_t virt_end) {
    uint32_t v;
    for (v = virt_start; v < virt_end; v += 0x1000) {
        uint32_t phys = paging_get_phys(v);
        if (phys) {
            pmm_free((void *)phys);
            paging_unmap(v);
        }
    }
}

uint32_t paging_get_phys(uint32_t virt) {
    pte_t *pt = get_page_table(virt, 0, 0);
    if (!pt) return 0;
    uint32_t pti = (virt >> 12) & 0x3FF;
    return pt[pti] & ~0xFFF;
}

void paging_init() {
    memset(page_directory, 0, sizeof(page_directory));

    // identity map first 4MB with user access
    uint32_t i;
    for (i = 0; i < 1024; i++)
        kernel_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    page_directory[0] = (uint32_t)kernel_page_table | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    // load page directory and enable paging
    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"(page_directory) : "eax"
    );

    vga_printf("Paging enabled\n");
}