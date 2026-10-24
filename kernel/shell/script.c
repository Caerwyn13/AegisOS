#include "script.h"
#include "aegisfs.h"
#include "string.h"
#include "vga.h"

static uint8_t script_buffer[FS_MAX_SIZE];
static char line_output[FS_MAX_SIZE];

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static int parse_print_line(const uint8_t *line, uint32_t length,
                            uint32_t *error_column) {
    uint32_t i = 0;
    while (i < length && is_space((char)line[i]))
        i++;

    if (i == length || line[i] == '#')
        return 0;

    if (length - i < 5 || line[i] != 'p' || line[i + 1] != 'r' ||
        line[i + 2] != 'i' || line[i + 3] != 'n' || line[i + 4] != 't' ||
        (i + 5 < length && !is_space((char)line[i + 5]) &&
         line[i + 5] != '"')) {
        *error_column = i + 1;
        return -1;
    }

    i += 5;
    while (i < length && is_space((char)line[i]))
        i++;
    if (i >= length || line[i] != '"') {
        *error_column = i + 1;
        return -2;
    }
    i++;

    uint32_t output_length = 0;
    int closed = 0;
    while (i < length) {
        char c = (char)line[i++];
        if (c == '"') {
            closed = 1;
            break;
        }
        if (c == '\\') {
            if (i >= length) {
                *error_column = i;
                return -3;
            }
            c = (char)line[i++];
            if (c == 'n')
                c = '\n';
            else if (c == 't')
                c = '\t';
            else if (c != '\\' && c != '"') {
                *error_column = i;
                return -3;
            }
        }
        line_output[output_length++] = c;
    }

    if (!closed) {
        *error_column = length + 1;
        return -4;
    }

    while (i < length && is_space((char)line[i]))
        i++;
    if (i < length && line[i] != '#') {
        *error_column = i + 1;
        return -5;
    }

    for (uint32_t j = 0; j < output_length; j++) {
        vga_putchar(line_output[j]);
        if (line_output[j] == '\n')
            vga_putchar('\r');
    }
    vga_putchar('\n');
    return 1;
}

int script_run_file(const char *filename) {
    uint32_t size = 0;
    if (fs_read(filename, script_buffer, &size) != 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "run: cannot read script: %s\n", filename);
        return -1;
    }

    uint32_t line_number = 1;
    uint32_t line_start = 0;
    while (line_start < size) {
        uint32_t line_end = line_start;
        while (line_end < size && script_buffer[line_end] != '\n')
            line_end++;

        uint32_t error_column = 0;
        int result = parse_print_line(script_buffer + line_start,
                                      line_end - line_start, &error_column);
        if (result < 0) {
            const char *reason = "invalid statement";
            if (result == -2)
                reason = "expected a quoted string after print";
            else if (result == -3)
                reason = "invalid escape sequence";
            else if (result == -4)
                reason = "missing closing quote";
            else if (result == -5)
                reason = "unexpected text after string";
            vga_printf_colour(LIGHT_RED, BLACK,
                              "run: %s:%u:%u: %s\n",
                              filename, line_number, error_column, reason);
            return -1;
        }

        line_start = line_end + (line_end < size ? 1 : 0);
        line_number++;
    }

    return 0;
}
