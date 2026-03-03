#include "keyboard.h"
#include "irq.h"
#include "ports.h"
#include "shell.h"
#include "vga.h"

#define KEYBOARD_PORT 0x60

#define SCANCODE_CAPS  0x3A
#define SCANCODE_LSHIFT 0x2A
#define SCANCODE_RSHIFT 0x36

static int caps_lock = 0;
static int shift     = 0;

static const char scancode_map[] = {
    0,   0,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '#',
    0,  '#', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,  ' '
};

static const char scancode_map_shift[] = {
    0,   0,  '!', '"', 0, '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '@', '~',
    0,   '~', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,  ' '
};

static char apply_caps(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int extended = 0;

static void keyboard_handler(registers_t *regs) {
    (void)regs;
    uint8_t scancode = inb(KEYBOARD_PORT);

    if (scancode == 0xE0) {
        extended = 1;
        return;
    }

    if (scancode == SCANCODE_LSHIFT || scancode == SCANCODE_RSHIFT) {
        shift = 1; return;
    }
    if (scancode == (SCANCODE_LSHIFT | 0x80) || scancode == (SCANCODE_RSHIFT | 0x80)) {
        shift = 0; return;
    }
    if (scancode == SCANCODE_CAPS) {
        caps_lock = !caps_lock; return;
    }

    if (scancode & 0x80) { extended = 0; return; }

    if (extended) {
        extended = 0;
        if (scancode == 0x48) { shell_handle_key(ARROW_UP);   return; }
        if (scancode == 0x50) { shell_handle_key(ARROW_DOWN); return; }
        return;
    }

    if (scancode < sizeof(scancode_map)) {
        char c = shift ? scancode_map_shift[scancode] : scancode_map[scancode];
        if (c) {
            if (caps_lock && c >= 'a' && c <= 'z') c = apply_caps(c);
            else if (caps_lock && shift && c >= 'A' && c <= 'Z') c = apply_caps(c);
            shell_handle_key(c);
        }
    }
}

void keyboard_init() {
	irq_register(1, keyboard_handler);
}
