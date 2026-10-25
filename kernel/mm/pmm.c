#include "pmm.h"
#include "serial.h"
#include "vga.h"

#define MAX_PAGES 0x100000    // 4 GB / 4096 
#define MAX_PHYSICAL_ADDRESS 0x100000000ULL
#define MULTIBOOT_MMAP_MIN_SIZE 20

static uint8_t bitmap[MAX_PAGES / 8];
static uint8_t allocated_bitmap[MAX_PAGES / 8];
static uint32_t free_pages = 0;
static uint32_t max_page = 0;

extern uint32_t kernel_end;

static void bitmap_set(uint32_t page)   { bitmap[page / 8] |= (1 << (page % 8)); }
static void bitmap_clear(uint32_t page) { bitmap[page / 8] &= ~(1 << (page % 8)); }
static int  bitmap_test(uint32_t page)  { return bitmap[page / 8] & (1 << (page % 8)); }
static void allocated_set(uint32_t page) {
    allocated_bitmap[page / 8] |= (1 << (page % 8));
}
static void allocated_clear(uint32_t page) {
    allocated_bitmap[page / 8] &= ~(1 << (page % 8));
}
static int allocated_test(uint32_t page) {
    return allocated_bitmap[page / 8] & (1 << (page % 8));
}

static uint64_t mmap_base(const mmap_entry_t *entry);
static uint64_t mmap_end(const mmap_entry_t *entry);

static void mark_page_used(uint32_t page) {
    if (page >= MAX_PAGES)
        return;
    if (!bitmap_test(page)) {
        bitmap_set(page);
        free_pages--;
    }
    allocated_clear(page);
}

static void mark_page_free(uint32_t page) {
    if (page >= MAX_PAGES)
        return;
    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages++;
    }
}

static int mmap_records_valid(uint32_t start, uint32_t map_length) {
    uint64_t end = (uint64_t)start + map_length;
    if (!start || !map_length || end > MAX_PHYSICAL_ADDRESS)
        return 0;

    uint32_t offset = 0;
    while (offset < map_length) {
        uint32_t remaining = map_length - offset;
        if (remaining < sizeof(uint32_t))
            return 0;

        const mmap_entry_t *entry =
            (const mmap_entry_t *)(start + offset);
        if (entry->size < MULTIBOOT_MMAP_MIN_SIZE ||
            entry->size > remaining - sizeof(uint32_t))
            return 0;
        uint64_t base = mmap_base(entry);
        uint64_t range_length =
            ((uint64_t)entry->length_high << 32) | entry->length_low;
        if (range_length > ~(uint64_t)0 - base)
            return 0;
        offset += sizeof(uint32_t) + entry->size;
    }
    return offset == map_length;
}

static uint64_t mmap_base(const mmap_entry_t *entry) {
    return ((uint64_t)entry->base_addr_high << 32) | entry->base_addr_low;
}

static uint64_t mmap_end(const mmap_entry_t *entry) {
    uint64_t base = mmap_base(entry);
    uint64_t length = ((uint64_t)entry->length_high << 32) |
                      entry->length_low;
    if (length > ~(uint64_t)0 - base)
        return ~(uint64_t)0;
    return base + length;
}

static void mark_physical_range(uint64_t start, uint64_t end, int usable) {
    if (start >= MAX_PHYSICAL_ADDRESS || start >= end)
        return;
    if (end > MAX_PHYSICAL_ADDRESS)
        end = MAX_PHYSICAL_ADDRESS;

    uint64_t first_page = usable
        ? (start + PAGE_SIZE - 1) / PAGE_SIZE
        : start / PAGE_SIZE;
    uint64_t end_page = usable
        ? end / PAGE_SIZE
        : (end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t page = (uint32_t)first_page;
         page < end_page && page < MAX_PAGES; page++) {
        if (usable)
            mark_page_free(page);
        else
            mark_page_used(page);
    }
    if (usable && end_page > max_page)
        max_page = (uint32_t)end_page;
}

static void mark_memory_map(const multiboot_info_t *mbi, int usable) {
    uint32_t offset = 0;
    while (offset < mbi->mmap_length) {
        const mmap_entry_t *entry =
            (const mmap_entry_t *)(mbi->mmap_addr + offset);
        uint64_t start = mmap_base(entry);
        uint64_t end = mmap_end(entry);
        if ((entry->type == 1) == usable)
            mark_physical_range(start, end, usable);
        offset += sizeof(uint32_t) + entry->size;
    }
}

// ============================================================
// Mark a physical page as used
// ============================================================
void pmm_set_used(uint32_t addr) {
    uint32_t page = addr / PAGE_SIZE;
    mark_page_used(page);
}

// ============================================================
// Mark a range of pages as used
// ============================================================
void pmm_reserve_range(uint32_t start, uint32_t size) {
    if (!size)
        return;
    uint64_t end = (uint64_t)start + size;
    mark_physical_range(start, end, 0);
}

// ============================================================
// Initialize PMM from multiboot memory map
// ============================================================
int pmm_init(multiboot_info_t* mbi) {
    for (uint32_t i = 0; i < MAX_PAGES / 8; i++) {
        bitmap[i] = 0xFF;
        allocated_bitmap[i] = 0;
    }
    free_pages = 0;
    max_page = 0;

    if (!mbi || !(mbi->flags & MULTIBOOT_INFO_HAS_MMAP) ||
        !mmap_records_valid(mbi->mmap_addr, mbi->mmap_length)) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "PMM: missing or invalid Multiboot memory map\n");
        return -1;
    }

    mark_memory_map(mbi, 1);
    mark_memory_map(mbi, 0);

    pmm_reserve_range(0, (uint32_t)&kernel_end);
    pmm_reserve_range((uint32_t)mbi, sizeof(*mbi));
    pmm_reserve_range(mbi->mmap_addr, mbi->mmap_length);

    if (mbi->flags & MULTIBOOT_INFO_HAS_MODULES) {
        uint64_t modules_size =
            (uint64_t)mbi->mods_count * sizeof(multiboot_module_t);
        uint64_t modules_end = (uint64_t)mbi->mods_addr + modules_size;
        if ((mbi->mods_count && !mbi->mods_addr) ||
            modules_size > 0xFFFFFFFFu ||
            modules_end > MAX_PHYSICAL_ADDRESS) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "PMM: invalid Multiboot module table\n");
            return -1;
        }
        pmm_reserve_range(mbi->mods_addr, (uint32_t)modules_size);
        const multiboot_module_t *modules =
            (const multiboot_module_t *)mbi->mods_addr;
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            if (modules[i].mod_end < modules[i].mod_start) {
                vga_printf_colour(LIGHT_RED, BLACK,
                                  "PMM: invalid Multiboot module range\n");
                return -1;
            }
            pmm_reserve_range(modules[i].mod_start,
                              modules[i].mod_end - modules[i].mod_start);
        }
    }

    vga_printf("PMM: %u free pages (%u MB)\n", free_pages, (free_pages * 4) / 1024);
    if (!free_pages) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "PMM: no usable memory remains after reservations\n");
        return -1;
    }
    return 0;
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
            allocated_set(i);
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
    uint32_t physical = (uint32_t)addr;
    if (!physical || physical % PAGE_SIZE)
        return;
    uint32_t page = physical / PAGE_SIZE;
    if (page >= max_page || !allocated_test(page))
        return;
    allocated_clear(page);
    mark_page_free(page);
}

uint32_t pmm_free_pages() { return free_pages; }