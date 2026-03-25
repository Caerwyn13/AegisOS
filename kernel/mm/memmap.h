#pragma once
#include "types.h"  // your own type definitions for uint32_t, etc.

/*
 * User-space virtual memory layout
 *
 * USER_START: virtual address where user processes begin
 * USER_END:   virtual address where user processes end
 *
 * Adjust USER_END as needed for total user-space memory.
 * Currently set to allow ~128 MB of user-space.
 */

#define USER_START 0x00400000   // matches hello.elf entry point
#define USER_END   0x08000000   // 128 MB limit for user processes

/*
 * Kernel memory layout (for reference)
 * - kernel code/data/bss/rodata: mapped in kernel page table
 * - kernel stack: mapped at high virtual address (0xFFC00000)
 */
#define KERNEL_STACK_VIRT 0xFFC00000
#define KERNEL_STACK_SIZE 0x10000  // 64 KB stack (may need to be adjusted)