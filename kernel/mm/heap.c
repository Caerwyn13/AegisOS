#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "vga.h"
#include "string.h"

#define HEAP_START 0x01000000  // 16MB
#define HEAP_MAX   0x04000000  // 64MB max
#define MIN_SPLIT  16

typedef struct block_t {
    uint32_t        size;
    uint8_t         free;
    struct block_t *next;
    struct block_t *prev;
} block_t;

static block_t *head    = 0;
static uint32_t heap_end = HEAP_START;

static int expand(uint32_t size) {
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t phys = (uint32_t)pmm_alloc();
        if (!phys) {
            vga_printf("Heap: PMM out of memory during expand!\n");
            return -1;
        }
        paging_map(heap_end, phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
        heap_end += PAGE_SIZE;
    }

    return 0;
}

void heap_init() {
    if (expand(0x1000) != 0) {
        vga_printf("Heap: failed to initialise\n");
        return;
    }

    head = (block_t*)HEAP_START;
    head->size = 0x1000 - sizeof(block_t);
    head->free = 1;
    head->next = 0;
    head->prev = 0;

    vga_printf("Heap initialised at 0x%x\n", HEAP_START);
}

void *kmalloc(uint32_t size) {
    if (!size || !head) return 0;
    size = (size + 3) & ~3;

    block_t *b = head;
    while (b) {
        if (b->free && b->size >= size) {
            if (b->size >= size + sizeof(block_t) + MIN_SPLIT) {
                block_t *split = (block_t*)((uint32_t)b + sizeof(block_t) + size);
                split->size = b->size - size - sizeof(block_t);
                split->free = 1;
                split->next = b->next;
                split->prev = b;
                if (b->next) b->next->prev = split;
                b->next = split;
                b->size = size;
            }
            b->free = 0;
            return (void*)((uint32_t)b + sizeof(block_t));
        }

        if (!b->next) {
            uint32_t old_heap_end = heap_end;
            if (expand(size + sizeof(block_t)) != 0) {
                return 0;
            }

            if (heap_end == old_heap_end) {
                return 0;
            }

            block_t *nb = (block_t*)((uint32_t)b + sizeof(block_t) + b->size);
            nb->size = heap_end - (uint32_t)nb - sizeof(block_t);
            nb->free = 1;
            nb->next = 0;
            nb->prev = b;
            b->next = nb;
        }
        b = b->next;
    }
    return 0;
}

void kfree(void *ptr) {
    if (!ptr) return;

    block_t *b = (block_t*)((uint32_t)ptr - sizeof(block_t));
    b->free = 1;

    if (b->next && b->next->free) {
        b->size += sizeof(block_t) + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }

    if (b->prev && b->prev->free) {
        b->prev->size += sizeof(block_t) + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

void heap_stats() {
    uint32_t free_bytes = 0, used_bytes = 0, blocks = 0;
    block_t *b = head;
    while (b) {
        blocks++;
        if (b->free) free_bytes += b->size;
        else used_bytes += b->size;
        b = b->next;
    }

    vga_printf("Heap stats:\n  Used: %u\n  Free: %u\n  Blocks: %u\n",
               used_bytes, free_bytes, blocks);
}
