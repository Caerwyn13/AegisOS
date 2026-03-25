#include "vga.h"
#include "ports.h"
#include "stdarg.h"
#include "types.h"

// ============================================================
// Text mode state
// ============================================================
static uint16_t *vga_text_buffer = (uint16_t *)0xB8000;
static int       cursor_x = 0;
static int       cursor_y = 0;
static uint8_t   text_colour = 0;

// ============================================================
// Graphics mode state
// ============================================================
static uint8_t *vga_graphics_buffer = (uint8_t *)0xA0000;
static uint8_t current_mode = VGA_MODE_TEXT;

// ============================================================
// Helpers
// ============================================================

static uint16_t make_text_entry(char c, uint8_t col) {
    return (uint16_t)c | ((uint16_t)col << 8);
}

static uint8_t make_colour(vga_colour_t fg, vga_colour_t bg) {
    return fg | (bg << 4);
}

static void scroll_text() {
    if (cursor_y >= VGA_TEXT_HEIGHT) {
        for (int i = 0; i < (VGA_TEXT_HEIGHT - 1) * VGA_TEXT_WIDTH; i++)
            vga_text_buffer[i] = vga_text_buffer[i + VGA_TEXT_WIDTH];
        for (int i = (VGA_TEXT_HEIGHT - 1) * VGA_TEXT_WIDTH; i < VGA_TEXT_HEIGHT * VGA_TEXT_WIDTH; i++)
            vga_text_buffer[i] = make_text_entry(' ', text_colour);
        cursor_y = VGA_TEXT_HEIGHT - 1;
    }
}

// Simple decimal printing
static void print_uint(uint32_t n, int width, int zero_pad) {
    char tmp[16];
    int i = 0;

    if (n == 0) tmp[i++] = '0';
    else {
        while (n > 0) {
            tmp[i++] = '0' + (n % 10);
            n /= 10;
        }
    }

    while (i < width)
        tmp[i++] = zero_pad ? '0' : ' ';

    for (int j = i - 1; j >= 0; j--)
        vga_putchar(tmp[j]);
}

// Hex printing
static void print_hex(uint32_t n) {
    char hex[9];
    int i = 7;
    hex[8] = 0;
    while (i >= 0) {
        hex[i--] = "0123456789ABCDEF"[n & 0xF];
        n >>= 4;
    }
    char *p = hex;
    while (*p == '0' && *(p + 1)) p++;
    while (*p) vga_putchar(*p++);
}

// Core printf logic
static void vga_printf_args(const char *fmt, va_list args) {
    while (*fmt) {
        if (*fmt != '%') { vga_putchar(*fmt++); continue; }
        fmt++;

        int left_align = 0, zero_pad = 0, width = 0;
        if (*fmt == '-') { left_align = 1; fmt++; }
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        switch (*fmt) {
            case 'd': {
                int n = va_arg(args, int);
                if (n < 0) { vga_putchar('-'); n = -n; }
                print_uint((uint32_t)n, width, zero_pad);
                break;
            }
            case 'u':
                print_uint(va_arg(args, uint32_t), width, zero_pad);
                break;
            case 'x':
                print_hex(va_arg(args, uint32_t));
                break;
            case 'c':
                vga_putchar((char)va_arg(args, int));
                break;
            case 's': {
                const char *s = va_arg(args, const char *);
                int len = 0;
                const char *tmp = s;
                while (*tmp++) len++;
                if (left_align) {
                    while (*s) vga_putchar(*s++);
                    while (len < width) { vga_putchar(' '); len++; }
                } else {
                    while (len < width) { vga_putchar(' '); len++; }
                    while (*s) vga_putchar(*s++);
                }
                break;
            }
            case '%':
                vga_putchar('%');
                break;
            default:
                vga_putchar('%');
                vga_putchar(*fmt);
                break;
        }
        fmt++;
    }
}

// ============================================================
// Text mode API
// ============================================================

void vga_update_cursor() {
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_x >= VGA_TEXT_WIDTH) cursor_x = VGA_TEXT_WIDTH - 1;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_y >= VGA_TEXT_HEIGHT) cursor_y = VGA_TEXT_HEIGHT - 1;

    uint16_t pos = cursor_y * VGA_TEXT_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_init() {
    current_mode = VGA_MODE_TEXT;
    text_colour = make_colour(WHITE, BLACK);
    cursor_x = 0;
    cursor_y = 0;
    vga_clear();
}

void vga_clear() {
    if (current_mode != VGA_MODE_TEXT) return;
    for (int i = 0; i < VGA_TEXT_WIDTH * VGA_TEXT_HEIGHT; i++)
        vga_text_buffer[i] = make_text_entry(' ', text_colour);
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

void vga_reset_cursor() {
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

void vga_set_colour(vga_colour_t fg, vga_colour_t bg) {
    text_colour = make_colour(fg, bg);
}

int vga_get_row() {
    return cursor_y;
}

void vga_putchar(char c) {
    if (current_mode != VGA_MODE_TEXT) return;

    if (c == '\n') { cursor_x = 0; cursor_y++; }
    else if (c == '\r') { cursor_x = 0; }
    else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            vga_text_buffer[cursor_y * VGA_TEXT_WIDTH + cursor_x] = make_text_entry(' ', text_colour);
        }
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else {
        vga_text_buffer[cursor_y * VGA_TEXT_WIDTH + cursor_x] = make_text_entry(c, text_colour);
        cursor_x++;
    }

    if (cursor_x >= VGA_TEXT_WIDTH) { cursor_x = 0; cursor_y++; }
    scroll_text();
    vga_update_cursor();
}

void vga_print(const char *str) {
    while (*str) vga_putchar(*str++);
}

void vga_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vga_printf_args(fmt, args);
    va_end(args);
}

void vga_printf_colour(vga_colour_t fg, vga_colour_t bg, const char *fmt, ...) {
    uint8_t old = text_colour;
    vga_set_colour(fg, bg);
    va_list args;
    va_start(args, fmt);
    vga_printf_args(fmt, args);
    va_end(args);
    text_colour = old;
}

// ============================================================
// Graphics mode API
// ============================================================

void vga_set_mode(uint8_t mode) {
    current_mode = mode;

    if (mode == VGA_MODE_13H) {
        // BIOS interrupt 0x10 AH=0x00 AL=0x13
        asm volatile(
            "int $0x10"
            :
            : "a"(0x0013)
        );
        vga_graphics_buffer = (uint8_t*)0xA0000;
    }
}

void vga_putpixel(uint32_t x, uint32_t y, uint8_t colour) {
    if (current_mode != VGA_MODE_13H) return;
    if (x >= VGA_13H_WIDTH || y >= VGA_13H_HEIGHT) return;
    vga_graphics_buffer[y * VGA_13H_WIDTH + x] = colour;
}

void vga_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t colour) {
    if (current_mode != VGA_MODE_13H) return;
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++)
            vga_putpixel(x + i, y + j, colour);
}

void vga_clear_graphics(uint8_t colour) {
    if (current_mode != VGA_MODE_13H) return;
    for (uint32_t y = 0; y < VGA_13H_HEIGHT; y++)
        for (uint32_t x = 0; x < VGA_13H_WIDTH; x++)
            vga_putpixel(x, y, colour);
}