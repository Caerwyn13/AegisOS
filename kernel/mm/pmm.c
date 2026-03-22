#include "pmm.h"
#include "serial.h"
#include "vga.h"

#define MAX_PAGES 0x100000    // 4 GB / 4096 

static uint8_t bitmap[MAX_PAGES / 8];
static uint32_t total_pages = 0;
static uint32_t free_pages = 0;
static uint32_t max_page = 0;     // NEW: highest valid page based on multiboot

extern uint32_t kernel_end;

static void bitmap_set(uint32_t page)   { bitmap[page / 8] |= (1 << (page % 8)); }
static void bitmap_clear(uint32_t page) { bitmap[page / 8] &= ~(1 << (page % 8)); }
static int  bitmap_test(uint32_t page)  { return bitmap[page / 8] & (1 << (page % 8)); }

// ============================================================
// Mark a physical page as used
// ============================================================
void pmm_set_used(uint32_t addr) {
    uint32_t page = addr / PAGE_SIZE;
    if (!bitmap_test(page)) {
        bitmap_set(page);
        free_pages--;
    }
}

// ============================================================
// Mark a range of pages as used
// ============================================================
void pmm_reserve_range(uint32_t start, uint32_t size) {
    uint32_t page_start = start / PAGE_SIZE;
    uint32_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;   // SAFE rounding
    for (uint32_t i = 0; i < page_count; i++)
        pmm_set_used((page_start + i) * PAGE_SIZE);
}

// ============================================================
// Initialize PMM from multiboot memory map
// ============================================================
void pmm_init(multiboot_info_t* mbi) {
    // 1. Mark all pages as used initially
    for (uint32_t i = 0; i < MAX_PAGES / 8; i++)
        bitmap[i] = 0xFF;

    // 2. Parse multiboot memory map
    mmap_entry_t* entry = (mmap_entry_t*)mbi->mmap_addr;
    mmap_entry_t* end   = (mmap_entry_t*)((uint32_t)mbi->mmap_addr + mbi->mmap_length);

    while (entry < end) {
        if (entry->type == 1) {  // usable RAM
            uint32_t base = entry->base_addr_low;
            uint32_t len  = entry->length_low;
            uint32_t page_start = base / PAGE_SIZE;
            uint32_t count = (len + PAGE_SIZE - 1) / PAGE_SIZE;  // SAFE rounding

            // Clear bitmap for usable pages
            for (uint32_t i = 0; i < count; i++) {
                bitmap_clear(page_start + i);
                free_pages++;
                total_pages++;
            }

            // Track highest valid page
            if (page_start + count > max_page)
                max_page = page_start + count;
        }

        entry = (mmap_entry_t*)((uint32_t)entry + entry->size + sizeof(entry->size));
    }

    // 3. Reserve kernel pages
    uint32_t kernel_pages = ((uint32_t)&kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = 0; i < kernel_pages; i++)
        pmm_set_used(i * PAGE_SIZE);

    // 4. Reserve page 0 (common practice)
    bitmap_set(0);

    vga_printf("PMM: %u free pages (%u MB)\n", free_pages, (free_pages * 4) / 1024);
}

// ============================================================
// Allocate a single page
// ============================================================
void* pmm_alloc() {
    if (free_pages == 0) {
        vga_printf("PMM: No free pages left!\n");
        return 0;
    }

    for (uint32_t i = 1; i < max_page; i++) {  // SCAN ONLY valid pages
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            return (void*)(i * PAGE_SIZE);
        }
    }

    vga_printf("PMM: Out of memory! (scanned max_page=%u)\n", max_page);
    return 0;
}

// ============================================================
// Free a page
// ============================================================
void pmm_free(void* addr) {
    uint32_t page = (uint32_t)addr / PAGE_SIZE;
    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages++;
    }
}

uint32_t pmm_free_pages() { return free_pages; }