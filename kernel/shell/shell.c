#include "shell.h"
#include "vga.h"
#include "string.h"
#include "pmm.h"
#include "ports.h"

#define BUFFER_SIZE 256
#define HISTORY_SIZE 10

// Command history
static char history[HISTORY_SIZE][BUFFER_SIZE];
static int  history_count = 0;
static int  history_index = -1;

static char buf[BUFFER_SIZE];
static int  buf_pos = 0;

static void history_add(const char *cmd) {
    if (history_count < HISTORY_SIZE) {
        strcpy(history[history_count++], cmd);
    } else {
        // shift history up
        int i;
        for (i = 0; i < HISTORY_SIZE - 1; i++)
            strcpy(history[i], history[i + 1]);
        strcpy(history[HISTORY_SIZE - 1], cmd);
    }
    history_index = history_count;
}

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
    vga_print_colour(" ________  _______   ________  ___  ________  ________  ________\n", LIGHT_CYAN, BLACK);
    vga_print_colour("|\\   __  \\|\\  ___ \\ |\\   ____\\|\\  \\|\\   ____\\|\\   __  \\|\\   ____\\\n", LIGHT_CYAN, BLACK);
    vga_print_colour("\\ \\  \\|\\  \\ \\   __/|\\ \\  \\___|\\ \\  \\ \\  \\___|.\\ \\  \\|\\  \\ \\  \\___|_\n", LIGHT_CYAN, BLACK);
    vga_print_colour(" \\ \\   __  \\ \\  \\_|/_\\ \\  \\  __\\ \\  \\ \\_____  \\ \\  \\\\\\  \\ \\_____  \\\n", LIGHT_CYAN, BLACK);
    vga_print_colour("  \\ \\  \\ \\  \\ \\  \\_|\\ \\ \\  \\|\\  \\ \\  \\|____|\\  \\ \\  \\\\\\  \\|____|\\  \\\n", LIGHT_CYAN, BLACK);
    vga_print_colour("   \\ \\__\\ \\__\\ \\_______\\ \\_______\\ \\__\\____\\_\\  \\ \\_______\\____\\_\\  \\\n", LIGHT_CYAN, BLACK);
    vga_print_colour("    \\|__|\\|__|\\|_______||\\|_______||\\|__|\\_________\\|_______|\\|_________\\\n", LIGHT_CYAN, BLACK);
    vga_print_colour("                                         \\|_________|        \\|_________|/\n", LIGHT_CYAN, BLACK);
    vga_print("\n");
    vga_print_colour("  Welcome to AegisOS!\n", YELLOW, BLACK);
    vga_print_colour("  Type 'help' for a list of commands.\n\n", LIGHT_GREY, BLACK);
    print_prompt();
}

void shell_handle_key(char c) {
    if (c == '\n') {
        vga_putchar('\n');
        buf[buf_pos] = 0;
        if (buf_pos > 0) {
            history_add(buf);
            execute(buf);
        }
        buf_pos = 0;
        history_index = history_count;
        print_prompt();
    } else if (c == '\b') {
        if (buf_pos > 0) {
            buf_pos--;
            vga_putchar('\b');
        }
    } else if (c == ARROW_UP) {
        if (history_index > 0) {
            history_index--;
            // clear current line
            while (buf_pos > 0) {
                buf_pos--;
                vga_putchar('\b');
            }
            strcpy(buf, history[history_index]);
            buf_pos = strlen(buf);
            vga_print(buf);
        }
    } else if (c == ARROW_DOWN) {
        if (history_index < history_count - 1) {
            history_index++;
            while (buf_pos > 0) {
                buf_pos--;
                vga_putchar('\b');
            }
            strcpy(buf, history[history_index]);
            buf_pos = strlen(buf);
            vga_print(buf);
        } else {
            // clear line
            while (buf_pos > 0) {
                buf_pos--;
                vga_putchar('\b');
            }
            history_index = history_count;
        }
    } else if (buf_pos < BUFFER_SIZE - 1) {
        buf[buf_pos++] = c;
        vga_print_colour((char[]){c, 0}, WHITE, BLACK);
    }
}