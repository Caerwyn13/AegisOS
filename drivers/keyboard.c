#include "keyboard.h"
#include "irq.h"
#include "ports.h"
#include "serial.h"

#define KEYBOARD_PORT 0x60

static const char scancode_map[] = {
	0, 0, '1','2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '#',
	0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
	'*', 0, ' '
};

static void keyboard_handler(registers_t* regs) {
	(void)regs;
	uint8_t scancode = inb(KEYBOARD_PORT);
	print("KEY\n");
	// Ignore key release (bit 7 set)
	if (scancode & 0x80) 
		return;

	if (scancode < sizeof(scancode_map)) {
		char c = scancode_map[scancode];
		if (c) {
			char str[2] = {c, 0};
			print(str);
		}
	}
}

void keyboard_init() {
	print("Registering Keyboard IRQ");
	irq_register(1, keyboard_handler);
	print("Keyboard IRQ Registered");
}
