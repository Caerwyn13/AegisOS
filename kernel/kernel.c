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

void kernel_main(multiboot_info_t* mbi) {
	serial_init();
	print("Hello World!\n");
	
	gdt_init();
	idt_init();
	pic_init();
	irq_init();
	keyboard_init();
	pmm_init(mbi);
	shell_init();
	__asm__ volatile ("sti");
	print("Kernel ready!\n");
	while (1);
}
