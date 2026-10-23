#pragma once
#include "types.h"

/* User-space virtual memory range. */

#define USER_START 0x00400000   // matches hello.elf entry point
#define USER_END   0x08000000   // 128 MB limit for user processes

/* Kernel stack layout. */
#define KERNEL_STACK_VIRT 0xFFC00000
#define KERNEL_STACK_SIZE 0x10000