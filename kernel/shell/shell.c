#include "shell.h"
#include "vga.h"
#include "string.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "pit.h"
#include "ports.h"
#include "multiboot.h"
#include "rtc.h"
#include "aegisfs.h"
#include "ata.h"
#include "elf.h"
#include "usermode.h"

#define BUFFER_SIZE     256
#define HISTORY_SIZE    10
#define MAX_ARGS        8
#define HELP_PAGE_SIZE  8
#define USER_STACK_SIZE 0x4000   // 16KB
#define USER_STACK_TOP  0x800000  // 8MB
// ============================================================
// State
// ============================================================

static multiboot_info_t *mbi_ptr      = 0;
static char              buf[BUFFER_SIZE];
static int               buf_pos      = 0;

static char history[HISTORY_SIZE][BUFFER_SIZE];
static int  history_count             = 0;
static int  history_index             = -1;

static char *args[MAX_ARGS];
static int   argc                     = 0;

static const char *help_lines[] = {
    "  help                   - show this message",
    "  help -p <page>         - show a specific page of help",
    "  clear                  - clear the screen",
    "  clear -c <colour>      - clear with background colour",
    "  echo <text>            - print text",
    "  mem                    - show free memory",
    "  memmap                 - show memory map",
    "  heap                   - shows the heap",
    "  uptime                 - show system uptime",
    "  date                   - show the current date and time",
    "  hexdump <addr> [len]   - dump memory in hex",
    "  history                - show command history",
    "  ls                     - list files",
    "  cat <file>             - read a file",
    "  write <file> <content> - write to a file",
    "  exec <file>            - execute an ELF binary",
    "  rm <file>              - delete a file",
    "  touch <file>           - create empty file",
    "  about                  - about AegisOS",
    "  reboot                 - reboot the system",
    "  shutdown               - shutdown the system",
    0  // null terminator
};

// ============================================================
// Helpers
// ============================================================

static void print_prompt() {
    vga_printf_colour(LIGHT_GREEN, BLACK, "root@AegisOS");
    vga_printf_colour(WHITE,       BLACK, ":~$ ");
    vga_set_colour(WHITE, BLACK);
}

static void history_add(const char *cmd) {
    if (history_count < HISTORY_SIZE) {
        strcpy(history[history_count++], cmd);
    } else {
        int i;
        for (i = 0; i < HISTORY_SIZE - 1; i++)
            strcpy(history[i], history[i + 1]);
        strcpy(history[HISTORY_SIZE - 1], cmd);
    }
    history_index = history_count;
}

static void parse_args(char *cmd) {
    argc = 0;
    char *p = cmd;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;
        args[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
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

static uint32_t parse_hex(const char *str) {
    uint32_t val = 0;
    const char *p = str;
    if (p[0] == '0' && p[1] == 'x') p += 2;
    while (*p) {
        val <<= 4;
        if      (*p >= '0' && *p <= '9') val |= *p - '0';
        else if (*p >= 'a' && *p <= 'f') val |= *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') val |= *p - 'A' + 10;
        p++;
    }
    return val;
}

static uint32_t parse_int(const char *str) {
    uint32_t val = 0;
    while (*str) val = val * 10 + (*str++ - '0');
    return val;
}

void shell_after_exec() {
    vga_printf("Process exited\n");
    print_prompt();
    // shell loop will continue naturally
}

// ============================================================
// DEBUG Commands
// ============================================================

static void _cmd_inodetest() {
    fs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    strcpy(inode.name, "test.txt");
    inode.size      = 42;
    inode.start_lba = 17;
    inode.used      = 1;

    // write raw inode to LBA 1
    uint8_t buf[512];
    memset(buf, 0, 512);
    memcpy(buf, &inode, sizeof(fs_inode_t));
    ata_write(1, 1, buf);

    // read it back
    uint8_t buf2[512];
    ata_read(1, 1, buf2);

    fs_inode_t inode2;
    memcpy(&inode2, buf2, sizeof(fs_inode_t));

    vga_printf("name: %s\n",  inode2.name);
    vga_printf("size: %u\n",  inode2.size);
    vga_printf("lba:  %u\n",  inode2.start_lba);
    vga_printf("used: %u\n",  inode2.used);
    vga_printf("sizeof: %u\n", sizeof(fs_inode_t));
}

static void _cmd_atatest() {
    uint8_t buf[512];
    // write a known pattern
    int i;
    for (i = 0; i < 512; i++)
        buf[i] = i & 0xFF;
    ata_write(1, 1, buf);

    // read it back
    uint8_t buf2[512];
    ata_read(1, 1, buf2);

    // check first 8 bytes
    for (i = 0; i < 8; i++)
        vga_printf("%x ", buf2[i]);
    vga_printf("\n");
}

static void _cmd_syscalltest() {
    __asm__ volatile (
        "mov %0, %%ebx\n"      // SYS_PRINT
        "mov $1, %%eax\n"     // String pointer
        "int $0x80\n"
        : : "r"("Syscalls work!\n")
        : "eax", "ebx"
    );
}

static void exec_returned() {
    vga_printf("Free pages after: %u\n", pmm_free_pages());
    paging_unmap_range(0x400000, 0x410000);
    // free user stack
    uint32_t i;
    for (i = 0; i < 8; i++)
        paging_unmap_range(0x8F000 - (i * 0x1000),
                           0x8F000 - (i * 0x1000) + 0x1000);
    vga_printf("Free pages freed: %u\n", pmm_free_pages());
    print_prompt();
}

// ============================================================
// Commands
// ============================================================

static void cmd_ls() {
    fs_list();
}

static void cmd_cat() {
    if (argc < 2) {
        vga_printf_colour(LIGHT_RED, BLACK, "Usage: cat <file>\n");
        return;
    }
    uint8_t *buf = (uint8_t *)kmalloc(FS_MAX_SIZE);
    if (!buf) { vga_printf("Out of memory\n"); return; }

    uint32_t size = 0;
    if (fs_read(args[1], buf, &size) < 0) {
        vga_printf_colour(LIGHT_RED, BLACK, "File not found: %s\n", args[1]);
    } else {
        buf[size] = 0;
        vga_printf("%s\n", (char *)buf);
    }
    kfree(buf);
}

static void cmd_write() {
    if (argc < 3) {
        vga_printf_colour(LIGHT_RED, BLACK, "Usage: write <file> <content>\n");
        return;
    }
    // join all args after filename into one string
    char buf[BUFFER_SIZE];
    memset(buf, 0, BUFFER_SIZE);
    int i;
    for (i = 2; i < argc; i++) {
        strcat(buf, args[i]);
        if (i < argc - 1) strcat(buf, " ");
    }
    if (fs_write(args[1], (uint8_t *)buf, strlen(buf)) < 0)
        vga_printf_colour(LIGHT_RED, BLACK, "Failed to write file\n");
    else
        vga_printf("Written %u bytes to %s\n", strlen(buf), args[1]);
}

extern kernel_func_t return_func;
extern void save_esp();

static void cmd_exec() {
    vga_printf("Free pages before: %u\n", pmm_free_pages());
    if (argc < 2) {
        vga_printf_colour(LIGHT_RED, BLACK, "Usage: exec <file>\n");
        return;
    }

    uint32_t entry = 0;
    if (elf_load(args[1], &entry) < 0) {
        vga_printf_colour(LIGHT_RED, BLACK, "Failed to load %s\n", args[1]);
        return;
    }

    uint32_t i;
    for (i = 0; i < 8; i++)
        paging_map(0x8F000 - (i * 0x1000),
                   0x8F000 - (i * 0x1000),
                   PAGE_PRESENT | PAGE_RW | PAGE_USER);

    save_esp();
    return_func = exec_returned;
    enter_usermode(entry, 0x8F000);
}

static void cmd_rm() {
    if (argc < 2) {
        vga_printf_colour(LIGHT_RED, BLACK, "Usage: rm <file>\n");
        return;
    }
    if (fs_delete(args[1]) < 0)
        vga_printf_colour(LIGHT_RED, BLACK, "File not found: %s\n", args[1]);
    else
        vga_printf("Deleted %s\n", args[1]);
}

static void cmd_touch() {
    if (argc < 2) {
        vga_printf_colour(LIGHT_RED, BLACK, "Usage: touch <file>\n");
        return;
    }
    if (fs_create(args[1]) < 0)
        vga_printf_colour(LIGHT_RED, BLACK, "Could not create file: %s\n", args[1]);
    else
        vga_printf("Created %s\n", args[1]);
}

static void cmd_help() {
    // count total lines
    int total = 0;
    while (help_lines[total]) total++;

    int total_pages = (total + HELP_PAGE_SIZE - 1) / HELP_PAGE_SIZE;
    int page = 1;

    // parse -p argument
    if (argc >= 3 && strcmp(args[1], "-p") == 0) {
        page = parse_int(args[2]);
        if (page < 1) page = 1;
        if (page > total_pages) page = total_pages;
    }

    int start = (page - 1) * HELP_PAGE_SIZE;
    int end   = start + HELP_PAGE_SIZE;
    if (end > total) end = total;

    vga_printf_colour(YELLOW, BLACK, "Available commands (page %d/%d):\n", page, total_pages);

    int i;
    for (i = start; i < end; i++)
        vga_printf("%s\n", help_lines[i]);

    if (total_pages > 1)
        vga_printf_colour(DARK_GREY, BLACK, "\nUse 'help -p <page>' to see more.\n");
}


static void cmd_clear() {
    if (argc >= 3 && (strcmp(args[1], "-c") == 0 || strcmp(args[1], "--colour") == 0)) {
        vga_set_colour(WHITE, parse_colour(args[2]));
    } else {
        vga_set_colour(WHITE, BLACK);
    }
    vga_clear();
}

static void cmd_echo() {
    int i;
    for (i = 1; i < argc; i++) {
        vga_printf("%s", args[i]);
        if (i < argc - 1) vga_putchar(' ');
    }
    vga_putchar('\n');
}

static void cmd_mem() {
    vga_printf("Free memory: %u KB\n", pmm_free_pages() * 4);
}

static void cmd_memmap() {
    if (!mbi_ptr) {
        vga_printf_colour(LIGHT_RED, BLACK, "No memory map available\n");
        return;
    }

    mmap_entry_t *entry = (mmap_entry_t *)mbi_ptr->mmap_addr;
    mmap_entry_t *end   = (mmap_entry_t *)(mbi_ptr->mmap_addr + mbi_ptr->mmap_length);

    vga_printf_colour(YELLOW,    BLACK, "%-16s %-16s %s\n", "Base", "Length", "Type");
    vga_printf_colour(DARK_GREY, BLACK, "%-16s %-16s %s\n", "----", "------", "----");

    while (entry < end) {
        vga_printf("0x%x        0x%x        ", entry->base_addr_low, entry->length_low);
        if (entry->type == 1)
            vga_printf_colour(LIGHT_GREEN, BLACK, "Available\n");
        else
            vga_printf_colour(LIGHT_RED,   BLACK, "Reserved\n");
        entry = (mmap_entry_t *)((uint32_t)entry + entry->size + sizeof(entry->size));
    }
}

static void cmd_heap() {
    heap_stats();
}

static void cmd_uptime() {
    uint32_t seconds = pit_ticks() / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours   = minutes / 60;
    seconds %= 60;
    minutes %= 60;
    vga_printf("Uptime: %uh %um %us\n", hours, minutes, seconds);
}

static void cmd_date() {
    rtc_time_t t = rtc_get_time();
    vga_printf("%02u/%02u/%u %02u:%02u:%02u\n",
        t.day, t.month, t.year,
        t.hour, t.minute, t.second);
}

static void cmd_hexdump() {
    if (argc < 2) {
        vga_printf_colour(LIGHT_RED, BLACK, "Usage: hexdump <address> [length]\n");
        return;
    }

    uint32_t addr = parse_hex(args[1]);
    uint32_t len  = argc >= 3 ? parse_int(args[2]) : 64;
    uint8_t *mem  = (uint8_t *)addr;
    uint32_t i;

    for (i = 0; i < len; i++) {
        if (i % 16 == 0)
            vga_printf_colour(DARK_GREY, BLACK, "0x%x  ", addr + i);
        vga_printf_colour(LIGHT_GREEN, BLACK, "%x ", mem[i]);
        if (i % 16 == 15) vga_putchar('\n');
    }
    if (len % 16 != 0) vga_putchar('\n');
}

static void cmd_history() {
    int i;
    for (i = 0; i < history_count; i++)
        vga_printf("%d  %s\n", i + 1, history[i]);
}

static void cmd_about() {
    vga_printf_colour(LIGHT_CYAN, BLACK, "========== ABOUT AegisOS ==========\n");
    vga_printf("A simple x86 OS written solely by ");
    vga_printf_colour(YELLOW, BLACK, "Caerwyn Sabin-Rees\n");
    vga_printf("This OS began as a simple creative project\n");
    vga_printf("but has now expanded into the time-consuming addiction it is today!\n");
}

static void cmd_shutdown() {
    //TODO: Fix this to work on real hardware
    // At the moment it only works for QEMU, not VMs or actual hardware
    outw(0x604, 0x2000);
}

static void cmd_reboot() {
    vga_printf("Rebooting...\n");
    uint8_t val = 0;
    while (val & 0x02)
        val = inb(0x64);
    outb(0x64, 0xFE); //TODO: FIX FOR VMs AND REAL HARDWARE
    while(1);
}

// ============================================================
// Command dispatch
// ============================================================

static void execute(char *cmd) {
    parse_args(cmd);
    if (argc == 0) return;

    if      (strcmp(args[0], "help")     == 0) cmd_help();
    else if (strcmp(args[0], "clear")    == 0) cmd_clear();
    else if (strcmp(args[0], "echo")     == 0) cmd_echo();
    else if (strcmp(args[0], "mem")      == 0) cmd_mem();
    else if (strcmp(args[0], "memmap")   == 0) cmd_memmap();
    else if (strcmp(args[0], "heap")     == 0) cmd_heap();
    else if (strcmp(args[0], "uptime")   == 0) cmd_uptime();
    else if (strcmp(args[0], "date")     == 0) cmd_date();
    else if (strcmp(args[0], "hexdump")  == 0) cmd_hexdump();
    else if (strcmp(args[0], "history")  == 0) cmd_history();
    else if (strcmp(args[0], "about")    == 0) cmd_about();
    else if (strcmp(args[0], "reboot")   == 0) cmd_reboot();
    else if (strcmp(args[0], "shutdown") == 0) cmd_shutdown();
    else if (strcmp(args[0], "ls")       == 0) cmd_ls();
    else if (strcmp(args[0], "cat")      == 0) cmd_cat();
    else if (strcmp(args[0], "write")    == 0) cmd_write();
    else if (strcmp(args[0], "rm")       == 0) cmd_rm();
    else if (strcmp(args[0], "touch")    == 0) cmd_touch();
    else if (strcmp(args[0], "exec")     == 0) cmd_exec();

    else if (strcmp(args[0], "atatest")     == 0) _cmd_atatest();
    else if (strcmp(args[0], "inodetest")   == 0) _cmd_inodetest();
    else if (strcmp(args[0], "syscalltest") == 0) _cmd_syscalltest();
    else {
        vga_printf_colour(LIGHT_RED, BLACK, "Unknown command: %s\n", args[0]);
    }
}

// ============================================================
// Public API
// ============================================================

void shell_init(multiboot_info_t *mbi) {
    mbi_ptr = mbi;
    //vga_clear();
    //TODO: FIX DISPLAY
    vga_printf_colour(LIGHT_CYAN, BLACK, "   _            _     ___  ___  \n");
    vga_printf_colour(LIGHT_CYAN, BLACK, "  /_\  ___ __ _(_)___/ _ \/ __| \n");
    vga_printf_colour(LIGHT_CYAN, BLACK, " / _ \/ -_) _` | (_-< (_) \__ \ \n");
    vga_printf_colour(LIGHT_CYAN, BLACK, "/_/ \_\___\__, |_/__/\___/|___/ \n");
    vga_printf_colour(LIGHT_CYAN, BLACK, "          |___/                 \n");      
    vga_printf("\n");
    vga_printf_colour(YELLOW,     BLACK, "  Welcome to AegisOS!\n");
    vga_printf_colour(LIGHT_GREY, BLACK, "  Type 'help' for a list of commands.\n\n");
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
            while (buf_pos > 0) { buf_pos--; vga_putchar('\b'); }
            strcpy(buf, history[history_index]);
            buf_pos = strlen(buf);
            vga_printf("%s", buf);
        }
    } else if (c == ARROW_DOWN) {
        if (history_index < history_count - 1) {
            history_index++;
            while (buf_pos > 0) { buf_pos--; vga_putchar('\b'); }
            strcpy(buf, history[history_index]);
            buf_pos = strlen(buf);
            vga_printf("%s", buf);
        } else {
            while (buf_pos > 0) { buf_pos--; vga_putchar('\b'); }
            history_index = history_count;
        }
    } else if (buf_pos < BUFFER_SIZE - 1) {
        buf[buf_pos++] = c;
        vga_putchar(c);
    }
}