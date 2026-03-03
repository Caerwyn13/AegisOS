#include "irq.h"
#include "idt.h"
#include "pic.h"
#include "serial.h"

static void (*handlers[16])(registers_t* regs);

void irq_register(int irq, void (*handler)(registers_t* regs)) {
	handlers[irq] = handler;
	print("Unmasking IRQ\n");
	pic_unmask(irq);
}

void irq_handler(registers_t* regs) {
	int irq = regs->int_no - 32;
	if (handlers[irq])
		handlers[irq](regs);
	pic_eoi(irq);
}

void irq_init() {
	idt_set_entry(32, (uint32_t)irq0,  0x08, 0x8E);
	idt_set_entry(33, (uint32_t)irq1,  0x08, 0x8E);
	idt_set_entry(34, (uint32_t)irq2,  0x08, 0x8E);
	idt_set_entry(35, (uint32_t)irq3,  0x08, 0x8E);
	idt_set_entry(36, (uint32_t)irq4,  0x08, 0x8E);
	idt_set_entry(37, (uint32_t)irq5,  0x08, 0x8E);
	idt_set_entry(38, (uint32_t)irq6,  0x08, 0x8E);
	idt_set_entry(39, (uint32_t)irq7,  0x08, 0x8E);
	idt_set_entry(40, (uint32_t)irq8,  0x08, 0x8E);
	idt_set_entry(41, (uint32_t)irq9,  0x08, 0x8E);
	idt_set_entry(42, (uint32_t)irq10, 0x08, 0x8E);
	idt_set_entry(43, (uint32_t)irq11, 0x08, 0x8E);
	idt_set_entry(44, (uint32_t)irq12, 0x08, 0x8E);
	idt_set_entry(45, (uint32_t)irq13, 0x08, 0x8E);
	idt_set_entry(46, (uint32_t)irq14, 0x08, 0x8E);
	idt_set_entry(47, (uint32_t)irq15, 0x08, 0x8E);
}
