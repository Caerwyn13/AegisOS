#include "editor.h"
#include "aegisfs.h"
#include "shell.h"
#include "string.h"
#include "vga.h"

#define EDITOR_BODY_ROWS 20
#define EDITOR_TEXT_WIDTH (VGA_TEXT_WIDTH - 1)

static char editor_buffer[FS_MAX_SIZE];
static char editor_filename[FS_MAX_NAME];
static char editor_message[64];
static uint32_t editor_size;
static uint32_t editor_cursor;
static uint32_t editor_top_line;
static uint32_t editor_left_column;
static int editor_dirty;
static int editor_active;
static int editor_quit_pending;

static uint32_t editor_line_start(uint32_t line) {
    uint32_t offset = 0;
    while (line && offset < editor_size) {
        if (editor_buffer[offset++] == '\n')
            line--;
    }
    return offset;
}

static uint32_t editor_cursor_line(void) {
    uint32_t line = 0;
    for (uint32_t i = 0; i < editor_cursor; i++) {
        if (editor_buffer[i] == '\n')
            line++;
    }
    return line;
}

static uint32_t editor_cursor_column(void) {
    uint32_t column = 0;
    for (uint32_t i = editor_cursor; i > 0; i--) {
        if (editor_buffer[i - 1] == '\n')
            break;
        column++;
    }
    return column;
}

static uint32_t editor_line_end(uint32_t start) {
    while (start < editor_size && editor_buffer[start] != '\n')
        start++;
    return start;
}

static uint32_t editor_line_count(void) {
    uint32_t lines = 1;
    for (uint32_t i = 0; i < editor_size; i++) {
        if (editor_buffer[i] == '\n')
            lines++;
    }
    return lines;
}

static void editor_render(void) {
    uint32_t cursor_line = editor_cursor_line();
    uint32_t cursor_column = editor_cursor_column();
    uint32_t line_count = editor_line_count();

    if (cursor_line < editor_top_line)
        editor_top_line = cursor_line;
    else if (cursor_line >= editor_top_line + EDITOR_BODY_ROWS)
        editor_top_line = cursor_line - EDITOR_BODY_ROWS + 1;

    if (cursor_column < editor_left_column)
        editor_left_column = cursor_column;
    else if (cursor_column >= editor_left_column + EDITOR_TEXT_WIDTH)
        editor_left_column = cursor_column - EDITOR_TEXT_WIDTH + 1;

    vga_clear();
    vga_set_cursor_position(0, 0);
    vga_printf_colour(WHITE, BLUE, " AegisOS Editor  |  %s", editor_filename);

    for (uint32_t row = 0; row < EDITOR_BODY_ROWS; row++) {
        uint32_t line = editor_top_line + row;
        uint32_t start = editor_line_start(line);
        vga_set_cursor_position((int)row + 1, 0);
        if (line >= line_count) {
            vga_printf_colour(LIGHT_BLUE, BLACK, "~");
            continue;
        }

        uint32_t end = editor_line_end(start);
        uint32_t column = 0;
        uint32_t shown = 0;
        for (uint32_t i = start; i < end; i++, column++) {
            if (column < editor_left_column)
                continue;
            if (shown == EDITOR_TEXT_WIDTH)
                break;
            vga_putchar(editor_buffer[i]);
            shown++;
        }
    }

    vga_set_cursor_position(21, 0);
    vga_printf_colour(BLACK, LIGHT_GREY, " %-31s %s ",
                      editor_filename, editor_dirty ? "Modified" : "Saved");
    vga_printf_colour(DARK_GREY, BLACK, "Line %u, Column %u",
                      cursor_line + 1, cursor_column + 1);

    vga_set_cursor_position(22, 0);
    if (editor_message[0])
        vga_printf_colour(YELLOW, BLACK, "%s", editor_message);

    vga_set_cursor_position(24, 0);
    vga_printf_colour(WHITE, DARK_GREY,
                      " ^S Save   ^Q Quit   Arrows Move   Home/End Line ");
    vga_set_cursor_position((int)(cursor_line - editor_top_line) + 1,
                            (int)(cursor_column - editor_left_column));
}

static void editor_close(void) {
    editor_active = 0;
    editor_quit_pending = 0;
    vga_clear();
    vga_printf_colour(LIGHT_GREY, BLACK, "Closed editor: %s\n", editor_filename);
}

static void editor_save(void) {
    if (fs_write(editor_filename, (const uint8_t *)editor_buffer,
                 editor_size) != 0) {
        strcpy(editor_message, "Save failed; your changes are still in memory.");
        return;
    }
    editor_dirty = 0;
    editor_quit_pending = 0;
    editor_message[0] = '\0';
}

static void editor_insert(char value) {
    if (editor_size >= FS_MAX_SIZE) {
        strcpy(editor_message, "File limit reached (32 KiB); cannot insert more text.");
        return;
    }
    for (uint32_t i = editor_size; i > editor_cursor; i--)
        editor_buffer[i] = editor_buffer[i - 1];
    editor_buffer[editor_cursor++] = value;
    editor_size++;
    editor_dirty = 1;
    editor_message[0] = '\0';
}

static void editor_backspace(void) {
    if (!editor_cursor)
        return;
    for (uint32_t i = editor_cursor - 1; i < editor_size - 1; i++)
        editor_buffer[i] = editor_buffer[i + 1];
    editor_cursor--;
    editor_size--;
    editor_dirty = 1;
    editor_message[0] = '\0';
}

static void editor_delete(void) {
    if (editor_cursor >= editor_size)
        return;
    for (uint32_t i = editor_cursor; i < editor_size - 1; i++)
        editor_buffer[i] = editor_buffer[i + 1];
    editor_size--;
    editor_dirty = 1;
    editor_message[0] = '\0';
}

static void editor_move_vertical(int direction) {
    uint32_t line = editor_cursor_line();
    if (direction < 0 && line == 0)
        return;
    uint32_t target_line = direction < 0 ? line - 1 : line + 1;
    if (target_line >= editor_line_count())
        return;
    uint32_t target_start = editor_line_start(target_line);

    uint32_t target_end = editor_line_end(target_start);
    uint32_t column = editor_cursor_column();
    uint32_t target_length = target_end - target_start;
    if (column > target_length)
        column = target_length;
    editor_cursor = target_start + column;
}

int editor_start(const char *filename) {
    uint32_t name_length = strlen(filename);
    if (!name_length || name_length >= FS_MAX_NAME)
        return -1;

    for (uint32_t i = 0; i < name_length; i++) {
        char c = filename[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return -1;
    }

    strcpy(editor_filename, filename);
    memset(editor_buffer, 0, sizeof(editor_buffer));
    editor_size = 0;
    editor_cursor = 0;
    editor_top_line = 0;
    editor_left_column = 0;
    editor_dirty = 0;
    editor_quit_pending = 0;
    editor_message[0] = '\0';

    if (fs_exists(filename)) {
        uint32_t size = 0;
        if (fs_read(filename, (uint8_t *)editor_buffer, &size) != 0)
            return -2;
        for (uint32_t i = 0; i < size; i++) {
            unsigned char c = (unsigned char)editor_buffer[i];
            if (c == '\r') {
                if (i + 1 < size && editor_buffer[i + 1] == '\n') {
                    for (uint32_t j = i; j + 1 < size; j++)
                        editor_buffer[j] = editor_buffer[j + 1];
                    size--;
                } else {
                    editor_buffer[i] = '\n';
                }
            } else if (c == '\t') {
                editor_buffer[i] = ' ';
            } else if (c != '\n' && (c < 32 || c > 126)) {
                return -3;
            }
        }
        editor_size = size;
    }

    editor_active = 1;
    editor_render();
    return 0;
}

int editor_is_active(void) {
    return editor_active;
}

void editor_handle_key(uint16_t key) {
    if (!editor_active)
        return;

    if (editor_quit_pending && key != KEY_CTRL_Q && key != KEY_CTRL_S) {
        editor_quit_pending = 0;
        editor_message[0] = '\0';
    }

    if (key == KEY_CTRL_S) {
        editor_quit_pending = 0;
        editor_save();
    } else if (key == KEY_CTRL_Q) {
        if (!editor_dirty || editor_quit_pending) {
            editor_close();
            return;
        }
        editor_quit_pending = 1;
        strcpy(editor_message,
               "Unsaved changes: Ctrl+Q again to discard, Ctrl+S to save.");
    } else if (key == '\n') {
        editor_insert('\n');
    } else if (key == '\b') {
        editor_backspace();
    } else if (key == KEY_DELETE) {
        editor_delete();
    } else if (key == ARROW_LEFT) {
        if (editor_cursor)
            editor_cursor--;
    } else if (key == ARROW_RIGHT) {
        if (editor_cursor < editor_size)
            editor_cursor++;
    } else if (key == ARROW_UP) {
        editor_move_vertical(-1);
    } else if (key == ARROW_DOWN) {
        editor_move_vertical(1);
    } else if (key == KEY_HOME) {
        editor_cursor = editor_line_start(editor_cursor_line());
    } else if (key == KEY_END) {
        editor_cursor = editor_line_end(editor_line_start(editor_cursor_line()));
    } else if (key == '\t') {
        for (int i = 0; i < 4; i++)
            editor_insert(' ');
    } else if (key >= 32 && key <= 126) {
        editor_insert((char)key);
    }

    editor_render();
}
