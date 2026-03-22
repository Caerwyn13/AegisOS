#ifndef PMM_H
#define PMM_H

#include "types.h"
#include "multiboot.h"

#define PAGE_SIZE 4096

void pmm_init(multiboot_info_t* mbi);
void* pmm_alloc();
void pmm_free(void* addr);
uint32_t pmm_free_pages();

// NEW
void pmm_set_used(uint32_t addr);
void pmm_reserve_range(uint32_t start, uint32_t size);

#endif // PMM_H