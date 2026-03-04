// Main display drivers
#include "types.h"
#include "vga.h"
#include "ports.h"
#include "stdarg.h"

#define VGA_ADDRESS 0xB8000

static uint16_t* vga  = (uint16_t*)VGA_ADDRESS;
static int cursor_x   = 0;
static int cursor_y   = 0;
static uint8_t colour = 0;

void vga_update_cursor() {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static uint16_t make_entry(char c, uint8_t col) {
    return (uint16_t)c | ((uint16_t)col << 8);
}

static uint8_t make_colour(vga_colour_t fg, vga_colour_t bg) {
    return fg | (bg << 4);
}

static void scroll() {
    if (cursor_y >= VGA_HEIGHT) {
        int i;
        for (i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
            vga[i] = vga[i + VGA_WIDTH];
        for (i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
            vga[i] = make_entry(' ', colour);
    }
}

void vga_init() {
    colour = make_colour(LIGHT_GREY, BLACK);
    vga_clear();
}

void vga_clear() {
    int i;
    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = make_entry(' ', colour);
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

void vga_set_colour(vga_colour_t fg, vga_colour_t bg) {
    colour = make_colour(fg, bg);
}

void vga_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            vga[cursor_y * VGA_WIDTH + cursor_x] = make_entry(' ', colour);
        }
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else {
        vga[cursor_y * VGA_WIDTH + cursor_x] = make_entry(c, colour);
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    scroll();
    vga_update_cursor();
}

void vga_print(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_print_colour(const char* str, vga_colour_t fg, vga_colour_t bg) {
    uint8_t old = colour;
    colour = make_colour(fg, bg);
    vga_print(str);
    colour = old;
}

void vga_print_int(uint32_t n) {
    char buf[16];
    int i = 0;
    if (n == 0) {
        vga_print("0");
        return;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    // reverse
    int start = 0, end = i - 1;
    while (start < end) {
        char t = buf[start];
        buf[start++] = buf[end];
        buf[end--] = t;
    }
    buf[i] = 0;
    vga_print(buf);
}

void vga_print_int_colour(uint32_t n, vga_colour_t fg, vga_colour_t bg) {
    uint8_t old = colour;
    vga_set_colour(fg, bg);
    vga_print_int(n);
    colour = old;
}

void vga_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd': {
                    int n = va_arg(args, int);
                    if (n < 0) { vga_putchar('-'); n = -n; }
                    vga_print_int((uint32_t)n);
                    break;
                }
                case 'u':
                    vga_print_int(va_arg(args, uint32_t));
                    break;
                case 'x': {
                    uint32_t n = va_arg(args, uint32_t);
                    char hex[9];
                    int i = 7;
                    hex[8] = 0;
                    while (i >= 0) {
                        hex[i--] = "0123456789ABCDEF"[n & 0xF];
                        n >>= 4;
                    }
                    // skip leading zeros
                    char *p = hex;
                    while (*p == '0' && *(p+1)) p++;
                    vga_print(p);
                    break;
                }
                case 'c':
                    vga_putchar((char)va_arg(args, int));
                    break;
                case 's':
                    vga_print(va_arg(args, const char *));
                    break;
                case '%':
                    vga_putchar('%');
                    break;
                default:
                    vga_putchar('%');
                    vga_putchar(*fmt);
                    break;
            }
        } else {
            vga_putchar(*fmt);
        }
        fmt++;
    }

    va_end(args);
}

void vga_printf_colour(vga_colour_t fg, vga_colour_t bg, const char *fmt, ...) {
    uint8_t old = colour;
    vga_set_colour(fg, bg);

    va_list args;
    va_start(args, fmt);

    // reuse vga_printf logic by building a small shim
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd': {
                    int n = va_arg(args, int);
                    if (n < 0) { vga_putchar('-'); n = -n; }
                    vga_print_int((uint32_t)n);
                    break;
                }
                case 'u':
                    vga_print_int(va_arg(args, uint32_t));
                    break;
                case 'x': {
                    uint32_t n = va_arg(args, uint32_t);
                    char hex[9];
                    int i = 7;
                    hex[8] = 0;
                    while (i >= 0) {
                        hex[i--] = "0123456789ABCDEF"[n & 0xF];
                        n >>= 4;
                    }
                    char *p = hex;
                    while (*p == '0' && *(p+1)) p++;
                    vga_print(p);
                    break;
                }
                case 'c':
                    vga_putchar((char)va_arg(args, int));
                    break;
                case 's':
                    vga_print(va_arg(args, const char *));
                    break;
                case '%':
                    vga_putchar('%');
                    break;
                default:
                    vga_putchar('%');
                    vga_putchar(*fmt);
                    break;
            }
        } else {
            vga_putchar(*fmt);
        }
        fmt++;
    }

    va_end(args);
    colour = old;
}