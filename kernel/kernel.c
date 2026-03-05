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
#include "rtc.h"
#include "paging.h"
#include "ata.h"
#include "aegisfs.h"
#include "heap.h"
#include "syscall.h"

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

	print("Initialisint the RTC...\n");
	rtc_init();

	print("Initializing keyboard driver and enabling keyboard input...\n");
	keyboard_init();

	print("Initializing Physical Memory Manager (PMM) using multiboot information...\n");
	pmm_init(mbi);

	print("Enabling the use of Paging...\n");
	paging_init();

	print("Initialising the heap...\n");
	heap_init();

	print("Enabling CPU interrupts (STI instruction)...\n");
	__asm__ volatile ("sti");

	pit_sleep(100);
	print("Initialising the ATA driver...\n");
	ata_init();

	pit_sleep(100);
	print("Initisalising the Filesystem...\n");
    fs_init();

	print("Initialising syscalls...\n");
	syscall_init();

	print("Starting basic kernel shell interface...\n");
	shell_init(mbi);

	print("Kernel initialization complete. System is now operational.\n");

	while (1);
}