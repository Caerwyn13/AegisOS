// kernel/kernel.c
#include "types.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "pic.h"
#include "serial.h"
#include "keyboard.h"

void kernel_main() {
	serial_init();
	print("Hello World!\n");
	
	gdt_init();
	idt_init();
	pic_init();
	irq_init();
	keyboard_init();
	__asm__ volatile ("sti");
	print("Kernel ready!\n");
	while (1);
}
