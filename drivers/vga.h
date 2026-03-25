#ifndef VGA_H
#define VGA_H

#include "types.h"
#include "stdarg.h"

// ------------------------
// Text mode constants
// ------------------------
#define VGA_TEXT_WIDTH  80
#define VGA_TEXT_HEIGHT 25

typedef enum {
    BLACK         = 0,
    BLUE          = 1,
    GREEN         = 2,
    CYAN          = 3,
    RED           = 4,
    MAGENTA       = 5,
    BROWN         = 6,
    LIGHT_GREY    = 7,
    DARK_GREY     = 8,
    LIGHT_BLUE    = 9,
    LIGHT_GREEN   = 10,
    LIGHT_CYAN    = 11,
    LIGHT_RED     = 12,
    LIGHT_MAGENTA = 13,
    YELLOW        = 14,
    WHITE         = 15,
} vga_colour_t;

// ------------------------
// Graphics mode constants
// ------------------------
#define VGA_MODE_TEXT 0x03
#define VGA_MODE_13H  0x13   // 320x200 256-color
#define VGA_13H_WIDTH 320
#define VGA_13H_HEIGHT 200

// ------------------------
// Text mode API
// ------------------------
void vga_init();
void vga_clear();
void vga_reset_cursor();
void vga_putchar(char c);
void vga_print(const char *str);
void vga_printf(const char *fmt, ...);
void vga_printf_colour(vga_colour_t fg, vga_colour_t bg, const char *fmt, ...);
void vga_set_colour(vga_colour_t fg, vga_colour_t bg);
void vga_update_cursor();
int  vga_get_row();

// ------------------------
// Graphics mode API
// ------------------------
void vga_set_mode(uint8_t mode);
void vga_putpixel(uint32_t x, uint32_t y, uint8_t colour);
void vga_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t colour);
void vga_clear_graphics(uint8_t colour);

#endif // VGA_H