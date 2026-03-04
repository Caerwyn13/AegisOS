// kernel/kernel.c
#include "types.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "pic.h"
#include "serial.h"
#include "keyboard.h"
#include "pmm.h"
#include "vga.h"
#include "shell.h"
#include "multiboot.h"
#include "pit.h"
#include "paging.h"

void kernel_main(multiboot_info_t* mbi) {
	print("Starting kernel initialization...\n");

	print("Initializing serial interface for debugging output...\n");
	serial_init();

	print("Setting up Global Descriptor Table (GDT) for memory segmentation...\n");
	gdt_init();

	print("Setting up Interrupt Descriptor Table (IDT) for interrupt handling...\n");
	idt_init();

	print("Configuring Programmable Interrupt Controller (PIC)...\n");
	pic_init();

	print("Installing IRQ handlers for hardware interrupts...\n");
	irq_init();

	print("Initialising the PIT Clock...\n");
	pit_init(1000);  // 1000hz = 1 tick per ms

	print("Initializing keyboard driver and enabling keyboard input...\n");
	keyboard_init();

	print("Initializing Physical Memory Manager (PMM) using multiboot information...\n");
	pmm_init(mbi);

	print("Enabling the use of Paging...");
	paging_init();

	print("Starting basic kernel shell interface...\n");
	shell_init(mbi);

	print("Enabling CPU interrupts (STI instruction)...\n");
	__asm__ volatile ("sti");

	print("Kernel initialization complete. System is now operational.\n");

	while (1);
}