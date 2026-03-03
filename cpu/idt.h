#ifndef IDT_H
#define IDT_H

#include "types.h"

typedef struct {
	uint16_t base_low;
	uint16_t sel;
	uint8_t  zero;
	uint8_t  flags;
	uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed)) idt_ptr_t;

void idt_set_entry(int i, uint32_t base, uint16_t sel, uint8_t flags);
void idt_init();

#endif // IDT_H
