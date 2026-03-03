#include "shell.h"
#include "vga.h"
#include "string.h"
#include "pmm.h"

#define BUFFER_SIZE 256

static char buf[BUFFER_SIZE];
static int  buf_pos = 0;

static void print_prompt() {
    vga_print_colour("root@AegisOS", LIGHT_GREEN, BLACK);
    vga_print_colour(":~$ ", WHITE, BLACK);
    vga_set_colour(WHITE, BLACK);
}

static void cmd_help() {
    vga_print_colour("Available commands:\n", YELLOW, BLACK);
    vga_print("  help  - show this message\n");
    vga_print("  clear - clear the screen\n");
    vga_print("  mem   - show free memory\n");
    vga_print("  about - about this OS\n");
}

static void cmd_clear() {
    vga_clear();
}

static void cmd_mem() {
    uint32_t pages = pmm_free_pages();
    uint32_t kb    = pages * 4;

    // Simple number printing
    char num[16];
    int i = 0;
    if (kb == 0) {
        num[i++] = '0';
    } else {
        uint32_t tmp = kb;
        int start = i; 
        while (tmp > 0) {
            num[i++] = '0' + (tmp % 10);
            tmp /= 10;
        }
        // Reverse
        int end = i - 1;
        while (start < end) {
            char t = num[start];
            num[start++] = num[end];
            num[end--] = t;
        }
    }
    num[i] = 0;

    vga_print("Free memory: ");
    vga_print(num);
    vga_print(" KB\n");
}

static void cmd_about() {
    vga_print_colour("========== ABOUT AegisOS ==========\n", LIGHT_CYAN, BLACK);
    vga_print("A simple x86 OS written solely by ");
    vga_print_colour("Caerwyn Sabin-Rees\n", YELLOW, BLACK);
    vga_print("This OS began as a simple creative project but has now expanded into the time-consuming addiction it is today!\n");
}

static void execute(const char* cmd) {
    if (strcmp(cmd, "help") == 0) cmd_help();
    else if (strcmp(cmd, "clear") == 0) cmd_clear();
    else if (strcmp(cmd, "mem") == 0) cmd_mem();
    else if (strcmp(cmd, "about") == 0) cmd_about();
    else {
        vga_print_colour("Unknown command: ", LIGHT_RED, BLACK);
        vga_print(cmd);
        vga_print("\n");
    }
}

void shell_init() {
    vga_clear();
    vga_print_colour("Welcome to AegisOS!\n\n", YELLOW, BLACK);
    print_prompt();
}

void shell_handle_key(char c) {
    if (c == '\n') {
        vga_putchar('\n');
        buf[buf_pos] = 0;
        if (buf_pos > 0)
            execute(buf);
        buf_pos = 0;
        print_prompt();
    } else if (c == '\b') {
        if (buf_pos > 0) {
            buf_pos--;
            vga_putchar('\b');
        }
    } else if (buf_pos < BUFFER_SIZE - 1) {
        buf[buf_pos++] = c;
        vga_print((char[]){c, 0});
    }
}