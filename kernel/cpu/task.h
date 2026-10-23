#ifndef TASK_H
#define TASK_H

#include "isr.h"

typedef void (*task_entry_t)(void *arg);

void task_init(void);
int task_create(task_entry_t entry, void *arg);
void task_yield(void);
registers_t *task_schedule(registers_t *regs);

#endif
