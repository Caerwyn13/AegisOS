#include "paging.h"
#include "pmm.h"
#include "vga.h"
#include "string.h"

// Page directory - 1024, each 4MB
static pde_t page_directory[1024] __attribute__((aligned(4096)));

// Keep one page table for the first 4MB (identity map the kernel)
static pte_t kernel_page_table[1024] __attribute__((aligned(4096)));

extern uint32_t kernel_end;

static void flush_tlb(uint32_t virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static pte_t* get_page_table(uint32_t virt, int create) {
    uint32_t pdi = virt >> 22;

    if (!(page_directory[pdi] & PAGE_PRESENT)) {
        if (!create) return 0;

        // Allocate a new page table
        uint32_t pt = (uint32_t)pmm_alloc();
        if (!pt) return 0;
        
        memset((void*)pt, 0, PAGE_SIZE);
        page_directory[pdi] = pt | PAGE_PRESENT | PAGE_RW;
    }

    return (pte_t*)(page_directory[pdi] & ~0xFFF);
}

void paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    pte_t* pt = get_page_table(virt, 1);
    if (!pt) {
        vga_printf("Paging: Failed to get page table for 0x%x\n", virt);
        return;
    }
    
    uint32_t pti = (virt >> 12) & 0x3FF;
    pt[pti] = (phys & ~0xFFF) | flags | PAGE_PRESENT;
    flush_tlb(virt);
}

void paging_unmap(uint32_t virt) {
    pte_t* pt = get_page_table(virt, 0);
    if (!pt) return;

    uint32_t pti = (virt >> 12) & 0x3FF;
    pt[pti] = 0;
    flush_tlb(virt);
}

uint32_t paging_get_phys(uint32_t virt) {
    pte_t* pt = get_page_table(virt, 0);
    if (!pt) return 0;

    uint32_t pti = (virt >> 12) & 0x3FF;
    return pt[pti] & ~0x3FF;
}

void paging_init() {
    // Clear page directory
    memset(page_directory, 0, sizeof(page_directory));

    // Identity map first 4MB (kernel)
    uint32_t i;
    for (i = 0; i < 1024; i++)
        kernel_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;

    // Put kernel page into directory
    page_directory[0] = (uint32_t)kernel_page_table | PAGE_PRESENT | PAGE_RW;

    // Map VGA buffer at 0xB8000
    paging_map(0xB8000, 0xB8000, PAGE_PRESENT | PAGE_RW);

    // Load page directory into CR3 and enable paging via CR0
    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"(page_directory) : "eax"
    );

    vga_printf("Paging enabled\n");
}