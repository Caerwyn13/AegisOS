#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGE_SIZE    4096
#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_USER    0x4

typedef uint32_t pte_t; // Page Table Entry
typedef uint32_t pde_t; // Page Directory Entry

void paging_init();
pte_t* get_page_table(uint32_t virt, int create, int user);
void paging_map(uint32_t virt, uint32_t phys, uint32_t flags);
void paging_unmap(uint32_t virt);
void paging_unmap_range(uint32_t virt_start, uint32_t virt_end);
int paging_map_user_range(uint32_t virt_start, uint32_t virt_end, int writable);
uint32_t paging_get_phys(uint32_t virt);

#endif // PAGING_H