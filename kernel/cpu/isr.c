#include "isr.h"
#include "serial.h"
#include "vga.h"

static const char *exceptions[] = {
	"Division By Zero",
	"Debug",
	"Non-Maskable Interrupt",
	"Breakpoint",
	"Overflow",
	"Out of Bounds",
	"Invalid Opcode",
	"No Coprocessor",
	"Double Fault",
	"Coprocessor Segment Overrun",
	"Bad TSS",
	"Segment Not Present",
	"Stack Fault",
	"General Protection Fault",
	"Page Fault",
	"Unknown Interrupt",
	"Coprocessor Fault",
	"Alignment Check",
	"Machine Check",
	"Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved",
	"Reserved", "Reserved", "Reserved", "Reserved"
};

void isr_handler(registers_t* regs) {
	// Extra info for General Protection Fault
	if (regs->int_no == 13) {
		vga_printf_colour(LIGHT_RED, BLACK, "GPF! err=0x%x eip=0x%x cs=0x%x\n",
			regs->err_code, regs->eip, regs->cs);
		for(;;);
	}

	if (regs->int_no == 14) {
		uint32_t fault_addr;
		__asm__ volatile ("mov %%cr2, %0" : "=r"(fault_addr));
		vga_printf_colour(LIGHT_RED, BLACK, "PAGE FAULT at 0x%x\n", fault_addr);
		vga_printf("err: %u eip: 0x%x cs: 0x%x esp: 0x%x\n",
			regs->err_code, regs->eip, regs->cs, regs->esp);
		for(;;);
	}
	print("EXCEPTION: ");
	print(exceptions[regs->int_no]);
	print("\n");
	for(;;);
}
