// Backup COM1 serial functions if VGA fails
#include "types.h"
#include "ports.h"

#define COM1 0x3F8

void serial_init() {
	outb(COM1 + 1, 0x00);	// Disable interrupts
	outb(COM1 + 3, 0x80);	// Enable DLAB
	outb(COM1 + 0, 0x03);	// Baud rate low byte (38400)
	outb(COM1 + 1, 0x00);   // Baud rate low byte
	outb(COM1 + 3, 0x03);	// 8 bits, no parity, one stop bit
	outb(COM1 + 2, 0xC7);	// Enable FIFO
	outb(COM1 + 4, 0x0B);	// Enable IRQs, RTS/DSR set
}

void print_char(char c) {
	while ((inb(COM1 + 5) & 0x20) == 0);
	outb(COM1, c);
}

void print(const char* str) {
	while (*str)
		print_char(*str++);
}
