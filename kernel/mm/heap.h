#ifndef HEAP_H
#define HEAP_H

#define HEAP_START 0x01000000  // 16MB mark
#define HEAP_MAX   0x04000000  // 64MB max
#define MIN_SPLIT  16          // minimum block size to split

#include "types.h"

void  heap_init();
void *kmalloc(uint32_t size);
void  kfree(void *ptr);
void  heap_stats();

#endif // HEAP_H