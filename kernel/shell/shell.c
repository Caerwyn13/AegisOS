#include "shell.h"
#include "vga.h"
#include "string.h"
#include "pmm.h"
#include "ports.h"

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
    vga_print("  shutdown - shutdown the system\n");
    vga_print("  clear - clear the screen\n");
    vga_print("  mem   - show free memory\n");
    vga_print("  about - about this OS\n");
}

static void cmd_clear() {
    vga_clear();
}

static void cmd_mem() {
    vga_print("Free memory: ");
    vga_print_int(pmm_free_pages() * 4);
    vga_print(" KB\n");
}

static void cmd_about() {
    vga_print_colour("========== ABOUT AegisOS ==========\n", LIGHT_CYAN, BLACK);
    vga_print("A simple x86 OS written solely by ");
    vga_print_colour("Caerwyn Sabin-Rees\n", YELLOW, BLACK);
    vga_print("This OS began as a simple creative project but has now expanded into the time-consuming addiction it is today!\n");
}

static void cmd_shutdown() {
    // QEMU specific shutdown via ACPI
    outw(0x604, 0x2000);
}

static void execute(const char* cmd) {
    if (strcmp(cmd, "help") == 0) cmd_help();
    else if (strcmp(cmd, "clear") == 0) cmd_clear();
    else if (strcmp(cmd, "shutdown") == 0) cmd_shutdown();
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