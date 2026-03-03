#ifndef VGA_H
#define VGA_H

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

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

void vga_init();
void vga_clear();
void vga_putchar(char c);
void vga_print(const char* str);
void vga_print_colour(const char* str, vga_colour_t fg, vga_colour_t bg);
void vga_set_colour(vga_colour_t fg, vga_colour_t bg);

#endif // VGA_H