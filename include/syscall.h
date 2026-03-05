#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

// Syscalls
#define SYS_EXIT    0
#define SYS_PRINT   1
#define SYS_READ    2
#define SYS_WRITE   3
#define SYS_OPEN    4
#define SYS_CLOSE   5
#define SYS_GETTIME 6

void syscall_init();

#endif // SYSCALL_H