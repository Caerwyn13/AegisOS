#include "shell.h"
#include "vga.h"
#include "string.h"
#include "pmm.h"
#include "ports.h"

#define BUFFER_SIZE 256
#define HISTORY_SIZE 10
#define MAX_ARGS 8

static multiboot_info_t *mbi_ptr = 0;

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

// Command-line arguments
static char *args[MAX_ARGS];
static int  argc = 0;

static void parse_args(char *cmd) {
    argc = 0;
    char *p = cmd;
    while (*p && argc < MAX_ARGS) {
        // skip spaces
        while (*p == ' ') p++;
        if (!*p) break;
        args[argc++] = p;
        // find end of argument
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
}

static void print_prompt() {
    vga_print_colour("root@AegisOS", LIGHT_GREEN, BLACK);
    vga_print_colour(":~$ ", WHITE, BLACK);
    vga_set_colour(WHITE, BLACK);
}

static void cmd_help() {
    vga_print_colour("Available commands:\n", YELLOW, BLACK);
    vga_print("  help                - show this message\n");
    vga_print("  clear               - clear the screen\n");
    vga_print("  clear -c <colour>   - clear with background colour\n");
    vga_print("  echo <text>         - print text\n");
    vga_print("  mem                 - show free memory\n");
    vga_print("  memmap              - show memory map\n");
    vga_print("  history             - show command history\n");
    vga_print("  about               - about this OS\n");
    vga_print("  reboot              - reboot the system\n");
    vga_print("  shutdown            - shutdown the system\n");
}
static vga_colour_t parse_colour(const char *str) {
    if (strcmp(str, "black")        == 0) return BLACK;
    if (strcmp(str, "blue")         == 0) return BLUE;
    if (strcmp(str, "green")        == 0) return GREEN;
    if (strcmp(str, "cyan")         == 0) return CYAN;
    if (strcmp(str, "red")          == 0) return RED;
    if (strcmp(str, "magenta")      == 0) return MAGENTA;
    if (strcmp(str, "brown")        == 0) return BROWN;
    if (strcmp(str, "grey")         == 0) return LIGHT_GREY;
    if (strcmp(str, "darkgrey")     == 0) return DARK_GREY;
    if (strcmp(str, "lightblue")    == 0) return LIGHT_BLUE;
    if (strcmp(str, "lightgreen")   == 0) return LIGHT_GREEN;
    if (strcmp(str, "lightcyan")    == 0) return LIGHT_CYAN;
    if (strcmp(str, "lightred")     == 0) return LIGHT_RED;
    if (strcmp(str, "lightmagenta") == 0) return LIGHT_MAGENTA;
    if (strcmp(str, "yellow")       == 0) return YELLOW;
    if (strcmp(str, "white")        == 0) return WHITE;
    return BLACK;
}

static void cmd_clear() {
    if (argc >= 3 && strcmp(args[1], "-c") == 0) {
        vga_colour_t bg = parse_colour(args[2]);
        vga_set_colour(WHITE, bg);
        vga_clear();
    } else if (argc >= 3 && strcmp(args[1], "--colour") == 0) {
        vga_colour_t bg = parse_colour(args[2]);
        vga_set_colour(WHITE, bg);
        vga_clear();
    } else {
        vga_set_colour(WHITE, BLACK);
        vga_clear();
    }
}

static void cmd_history() {
    int i;
    for (i = 0; i < history_count; i++) {
        vga_print_int(i + 1);
        vga_print("  ");
        vga_print(history[i]);
        vga_putchar('\n');
    }
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

static void cmd_echo() {
    int i;
    for (i = 1; i < argc; i++) {
        vga_print(args[i]);
        if (i < argc - 1) vga_putchar(' ');
    }
    vga_putchar('\n');
}

static void cmd_reboot() {
    vga_print("Rebooting...\n");
    // pulse the PS/2 reset line
    uint8_t val = 0;
    while (val & 0x02)
        val = inb(0x64);
    outb(0x64, 0xFE);
    while(1);
}

static void cmd_memmap() {
    if (!mbi_ptr) {
        vga_print("No memory map available\n");
        return;
    }

    mmap_entry_t *entry = (mmap_entry_t *)mbi_ptr->mmap_addr;
    mmap_entry_t *end   = (mmap_entry_t *)(mbi_ptr->mmap_addr + mbi_ptr->mmap_length);

    vga_print_colour("Base            Length          Type\n", YELLOW, BLACK);
    vga_print_colour("----            ------          ----\n", DARK_GREY, BLACK);

    while (entry < end) {
        // print base address
        vga_print("0x");
        vga_print_int(entry->base_addr_low);
        vga_print("        0x");
        vga_print_int(entry->length_low);
        vga_print("        ");

        if (entry->type == 1)
            vga_print_colour("Available\n", LIGHT_GREEN, BLACK);
        else
            vga_print_colour("Reserved\n",  LIGHT_RED, BLACK);

        entry = (mmap_entry_t *)((uint32_t)entry + entry->size + sizeof(entry->size));
    }
}

static void execute(char *cmd) {
    parse_args(cmd);
    if (argc == 0) return;

    if      (strcmp(args[0], "help")     == 0) cmd_help();
    else if (strcmp(args[0], "clear")    == 0) cmd_clear();
    else if (strcmp(args[0], "mem")      == 0) cmd_mem();
    else if (strcmp(args[0], "about")    == 0) cmd_about();
    else if (strcmp(args[0], "shutdown") == 0) cmd_shutdown();
    else if (strcmp(args[0], "echo")     == 0) cmd_echo();
    else if (strcmp(args[0], "reboot")   == 0) cmd_reboot();
    else if (strcmp(args[0], "memmap")   == 0) cmd_memmap();
    else if (strcmp(args[0], "history")  == 0) cmd_history();
    else {
        vga_print_colour("Unknown command: ", LIGHT_RED, BLACK);
        vga_print(args[0]);
        vga_print("\n");
    }
}

void shell_init(multiboot_info_t *mbi) {
    mbi_ptr = mbi;
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