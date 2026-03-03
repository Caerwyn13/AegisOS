#include "pic.h"
#include "ports.h"

void pic_init() {
	// ICW1 - start init
	outb(PIC1_COMMAND, 0x11);
	outb(PIC2_COMMAND, 0x11);

	// ICW2 - remap offsets
	outb(PIC1_DATA, 0x20);	// PIC1 starts at IRQ 32
	outb(PIC2_DATA, 0x28);	// PIC2 starts at IRQ 40
	
	// ICW3 - cascading
	outb(PIC1_DATA, 0x04);
	outb(PIC2_DATA, 0x02);

	// ICW4 - 8086 mode
	outb(PIC1_DATA, 0x01);
	outb(PIC1_DATA, 0x01);

	// Mask all interrupts for now
	outb(PIC1_DATA, 0xFF);
	outb(PIC2_DATA, 0xFF);
}

void pic_eoi(unsigned char irq) {
	if (irq >= 8)
		outb(PIC2_COMMAND, PIC2_EOI);
	outb(PIC1_COMMAND, PIC2_EOI);
}

void pic_unmask(uint8_t irq) {
	uint16_t port;
	uint8_t val;
	if (irq < 9) {
		port = PIC1_DATA;
	} else {
		port = PIC2_DATA;
		irq -= 8;
	}
	val = inb(port) & ~(1 << irq);
	outb(port, val);
}

void pic_mask(uint8_t irq) {
	uint16_t port;
	uint8_t val;
	if (irq < 8) {
		port = PIC1_DATA;
	} else {
		port = PIC2_DATA;
		irq -= 8;
	}
	val = inb(port) | (1 << irq);
	outb(port, val);
}

