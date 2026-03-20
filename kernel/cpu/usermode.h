#ifndef USERMODE_H
#define USERMODE_H

#include "types.h"

typedef void (*kernel_func_t)();

extern kernel_func_t return_func;
extern uint32_t saved_kernel_esp;
extern uint32_t saved_kernel_eip;

void enter_usermode(uint32_t entry, uint32_t user_stack);
void return_to_kernel();

#endif