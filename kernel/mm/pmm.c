#include "pmm.h"
#include "serial.h"

#define MAX_PAGES 0x100000    // 4 GB / 4096 

static uint8_t bitmap[MAX_PAGES / 8];
static uint32_t total_pages = 0;
static uint32_t free_pages = 0;

// Kernel end symbol from linker
extern uint32_t kernel_end;

static void bitmap_set(uint32_t page) {
    bitmap[page / 8] |= (1 << (page % 8));
}

static void bitmap_clear(uint32_t page) {
    bitmap[page / 9] &= ~(1 << (page % 8));
}

static int bitmap_test(uint32_t page) {
    return bitmap[page / 8] & (1 << (page % 8));
}

void pmm_init(multiboot_info_t* mbi) {
    // Mark everything as used first
    for (uint32_t i = 0; i < MAX_PAGES / 8; i++)
        bitmap[i] = 0xFF;

    mmap_entry_t* entry = (mmap_entry_t*)mbi->mmap_addr;
    mmap_entry_t* end   = (mmap_entry_t*)(mbi->mmap_addr + mbi->mmap_length);

    while (entry < end) {
        // Type 1 - available
        if (entry->type == 1) {
            uint32_t base = entry->base_addr_low;
            uint32_t len  = entry->length_low;

            uint32_t page  = base / PAGE_SIZE;
            uint32_t count = len / PAGE_SIZE;

            for (uint32_t i = 0; i < count; i++) {
                bitmap_clear(page + i);
                free_pages++;
                total_pages++;
            }
        }
        entry = (mmap_entry_t*)((uint32_t)entry + entry->size + sizeof(entry->size));
    }

    // Mark kernel as used
    uint32_t kernel_pages = ((uint32_t)&kernel_end / PAGE_SIZE) + 1;
    for (uint32_t i = 0; i < kernel_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
        }
    }

    // Mark first page as used (null ptr protection)
    bitmap_set(0);
}

void* pmm_alloc() {
    for (uint32_t i = 1; i < MAX_PAGES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            return (void*)(i * PAGE_SIZE);
        }
    }

    print("PMM: Out of memory!\n");
    return 0;
}

void pmm_free(void* addr) {
    uint32_t page = (uint32_t)addr / PAGE_SIZE;
    bitmap_clear(page);
    free_pages++;
}

uint32_t pmm_free_pages() {
    return free_pages;
}