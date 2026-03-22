#include "paging.h"
#include "pmm.h"
#include "vga.h"
#include "string.h"

#define EXTRA_PAGE_TABLES 8

// ============================================================
// State
// ============================================================
static pde_t page_directory[1024] __attribute__((aligned(4096)));
static pte_t kernel_page_table[1024] __attribute__((aligned(4096)));
static pte_t extra_page_tables[EXTRA_PAGE_TABLES][1024] __attribute__((aligned(4096)));
static int extra_page_table_used[EXTRA_PAGE_TABLES] = {0};

extern uint32_t kernel_end;

// ============================================================
// Helpers
// ============================================================

static void flush_tlb(uint32_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static pte_t* alloc_page_table() {
    for (int i = 0; i < EXTRA_PAGE_TABLES; i++) {
        if (!extra_page_table_used[i]) {
            extra_page_table_used[i] = 1;
            memset(extra_page_tables[i], 0, PAGE_SIZE);
            pmm_reserve_range((uint32_t)extra_page_tables[i], PAGE_SIZE);
            return extra_page_tables[i];
        }
    }

    uint32_t phys = (uint32_t)pmm_alloc();
    if (!phys) {
        vga_printf("Paging: out of PMM pages for page tables!\n");
        return 0;
    }

    // Temporary kernel mapping for initialization
    uint32_t temp_virt = 0xF0000000; // safely unused area
    paging_map(temp_virt, phys, PAGE_PRESENT | PAGE_RW);
    memset((void*)temp_virt, 0, PAGE_SIZE);
    return (pte_t*)temp_virt;
}

pte_t* get_page_table(uint32_t virt, int create, int user) {
    uint32_t pdi = virt >> 22;

    if (!(page_directory[pdi] & PAGE_PRESENT)) {
        if (!create) return 0;

        pte_t* pt = alloc_page_table();
        if (!pt) return 0;

        page_directory[pdi] = ((uint32_t)pt & ~0xFFF) | PAGE_PRESENT | PAGE_RW | (user ? PAGE_USER : 0);
    }

    return (pte_t*)(page_directory[pdi] & ~0xFFF);
}

// ============================================================
// Public API
// ============================================================

void paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    int user = (flags & PAGE_USER) != 0;
    pte_t* pt = get_page_table(virt, 1, user);
    if (!pt) {
        vga_printf("Paging: failed to get page table for 0x%x\n", virt);
        return;
    }

    uint32_t pti = (virt >> 12) & 0x3FF;
    pt[pti] = (phys & ~0xFFF) | (flags & 0xFFF);
    flush_tlb(virt);
}

int paging_map_user_range(uint32_t virt_start, uint32_t virt_end, int writable) {
    for (uint32_t v = virt_start; v < virt_end; v += 4096) {
        uint32_t phys = (uint32_t)pmm_alloc();
        if (!phys) return -1;

        // Temporarily make all pages readable
        // I hate memory management
        //uint32_t flags = PAGE_PRESENT | PAGE_USER | (writable ? PAGE_RW : 0);
        uint32_t flags = PAGE_PRESENT | PAGE_USER | PAGE_RW;
        paging_map(v, phys, flags);
    }
    return 0;
}

void paging_unmap(uint32_t virt) {
    pte_t* pt = get_page_table(virt, 0, 0);
    if (!pt) return;

    uint32_t pti = (virt >> 12) & 0x3FF;
    uint32_t phys = pt[pti] & ~0xFFF;

    if (phys) {
        pmm_free((void*)phys);
    }

    pt[pti] = 0;
    flush_tlb(virt);
}

void paging_unmap_range(uint32_t virt_start, uint32_t virt_end) {
    for (uint32_t v = virt_start; v < virt_end; v += 4096)
        paging_unmap(v);
}

uint32_t paging_get_phys(uint32_t virt) {
    pte_t* pt = get_page_table(virt, 0, 0);
    if (!pt) return 0;
    uint32_t pti = (virt >> 12) & 0x3FF;
    return pt[pti] & ~0xFFF;
}

void paging_init() {
    pmm_reserve_range((uint32_t)page_directory, sizeof(page_directory));
    pmm_reserve_range((uint32_t)kernel_page_table, sizeof(kernel_page_table));
    pmm_reserve_range((uint32_t)extra_page_tables, sizeof(extra_page_tables));

    memset(page_directory, 0, sizeof(page_directory));

    for (uint32_t i = 0; i < 1024; i++) {
        kernel_page_table[i] = (i * 4096) | PAGE_PRESENT | PAGE_RW;
        pmm_set_used(i * 4096);
    }
    page_directory[0] = (uint32_t)kernel_page_table | PAGE_PRESENT | PAGE_RW;

    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"(page_directory) : "eax"
    );

    vga_printf("Paging enabled\n");
}
