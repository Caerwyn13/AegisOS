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
#include "acpi.h"
#include "task.h"
#include "serial.h"
#include "net.h"

#define BUFFER_SIZE     256
#define HISTORY_SIZE    10
#define MAX_ARGS        8
#define HELP_PAGE_SIZE  8
#define KEY_QUEUE_SIZE  128
// ============================================================
// State
// ============================================================

static multiboot_info_t *mbi_ptr      = 0;
static char              buf[BUFFER_SIZE];
static int               buf_pos      = 0;
static volatile uint32_t key_read_index;
static volatile uint32_t key_write_index;
static char key_queue[KEY_QUEUE_SIZE];

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
    "  rps <choice>           - play rock-paper-scissors",
    "  taskdemo               - demonstrate multitasking",
    "  net                    - show network interface status",
    "  ping <IPv4|hostname>   - ping an IPv4 address or hostname",
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

static void cmd_exec() {
    if (argc < 2) {
        vga_printf_colour(LIGHT_RED, BLACK, "Usage: exec <file>\n");
        return;
    }

    uint32_t entry = 0;
    if (elf_load(args[1], &entry) < 0) {
        vga_printf_colour(LIGHT_RED, BLACK, "Failed to load %s\n", args[1]);
        return;
    }

    vga_printf("Entry Point: 0x%x\n", entry);
    vga_printf("Starting usermode...\n");
    
    enter_usermode(entry, USER_STACK_TOP);
    paging_unmap_range(0x00400000, 0x00410000);
    paging_unmap_range(USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP);
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

static void cmd_rps() {
    static const char *choices[] = {"rock", "paper", "scissors"};

    if (argc != 2) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "Usage: rps <rock|paper|scissors>\n");
        return;
    }

    int player;
    if      (strcmp(args[1], "rock") == 0)     player = 0;
    else if (strcmp(args[1], "paper") == 0)    player = 1;
    else if (strcmp(args[1], "scissors") == 0) player = 2;
    else {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "Choose rock, paper, or scissors\n");
        return;
    }

    int computer = pit_ticks() % 3;
    vga_printf("You: %s | AegisOS: %s\n", choices[player], choices[computer]);

    if (player == computer)
        vga_printf_colour(YELLOW, BLACK, "It's a tie!\n");
    else if ((player + 1) % 3 == computer)
        vga_printf_colour(LIGHT_RED, BLACK, "AegisOS wins!\n");
    else
        vga_printf_colour(LIGHT_GREEN, BLACK, "You win!\n");
}

static void task_demo_worker(void *arg) {
    const char *name = (const char *)arg;
    for (int i = 0; i < 5; i++) {
        print("Task ");
        print(name);
        print(" is running\n");
        pit_sleep(250);
    }
}

static void cmd_taskdemo() {
    if (task_create(task_demo_worker, "A") < 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "Could not start demo task (task slots may be full)\n");
        return;
    }

    if (task_create(task_demo_worker, "B") < 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "Task A started, but no slot was available for task B\n");
        return;
    }

    vga_printf("Started two kernel tasks. Watch the serial output for their turns.\n");
}

static void print_ipv4(uint32_t address) {
    vga_printf("%u.%u.%u.%u",
               (address >> 24) & 0xFF,
               (address >> 16) & 0xFF,
               (address >> 8) & 0xFF,
               address & 0xFF);
}

static void cmd_net() {
    if (argc != 1) {
        vga_printf_colour(LIGHT_RED, BLACK, "Usage: net\n");
        return;
    }

    net_interface_info_t info;
    net_get_interface(&info);
    if (!info.available) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "No supported network interface found (QEMU e1000 required)\n");
        return;
    }

    vga_printf("Interface: e1000\nMAC: ");
    for (uint32_t i = 0; i < sizeof(info.mac); i++) {
        if (i)
            vga_putchar(':');
        if (info.mac[i] < 16)
            vga_putchar('0');
        vga_printf("%x", info.mac[i]);
    }
    vga_printf("\nIPv4: ");
    print_ipv4(info.ip_address);
    vga_printf("\nNetmask: ");
    print_ipv4(info.netmask);
    vga_printf("\nGateway: ");
    print_ipv4(info.gateway);
    vga_printf("\nDNS: ");
    if (info.dns_server)
        print_ipv4(info.dns_server);
    else
        vga_printf("not provided by DHCP");
    vga_putchar('\n');
}

static void cmd_ping() {
    if (argc != 2) {
        vga_printf_colour(LIGHT_RED, BLACK, "Usage: ping <IPv4|hostname>\n");
        return;
    }

    uint32_t destination;
    int is_ipv4 = net_parse_ipv4(args[1], &destination) == 0;

    net_interface_info_t info;
    net_get_interface(&info);
    if (!info.available) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "ping: no supported network interface (QEMU e1000)\n");
        return;
    }

    if (!is_ipv4) {
        if (net_resolve_ipv4(args[1], &destination) != 0) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "ping: could not resolve hostname: %s\n", args[1]);
            return;
        }
    }

    vga_printf("PING %s (", args[1]);
    print_ipv4(destination);
    vga_printf("): 56 data bytes\n");

    net_ping_result_t result;
    int status = net_ping_ipv4(destination, &result);
    if (status == -2) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "ping: destination could not be reached (ARP timeout)\n");
        return;
    }
    if (status != 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "ping: network request failed (error %d)\n", status);
        return;
    }

    uint32_t total_ms = 0;
    uint32_t min_ms = 0xFFFFFFFF;
    uint32_t max_ms = 0;
    for (uint32_t i = 0; i < NET_PING_COUNT; i++) {
        if (!(result.reply_mask & (1u << i)))
            continue;
        vga_printf("64 bytes from ");
        print_ipv4(destination);
        vga_printf(": icmp_seq=%u time=%u ms\n", i, result.rtt_ms[i]);
        total_ms += result.rtt_ms[i];
        if (result.rtt_ms[i] < min_ms)
            min_ms = result.rtt_ms[i];
        if (result.rtt_ms[i] > max_ms)
            max_ms = result.rtt_ms[i];
    }

    vga_printf("\n--- ");
    print_ipv4(destination);
    vga_printf(" ping statistics ---\n%u packets transmitted, %u received, %u%% packet loss\n",
               NET_PING_COUNT, result.received,
               ((NET_PING_COUNT - result.received) * 100) / NET_PING_COUNT);
    if (result.received)
        vga_printf("rtt min/avg/max = %u/%u/%u ms\n",
                   min_ms, total_ms / result.received, max_ms);
}

static void cmd_about() {
    vga_printf_colour(LIGHT_CYAN, BLACK, "========== ABOUT AegisOS ==========\n");
    vga_printf("A simple x86 OS written solely by ");
    vga_printf_colour(YELLOW, BLACK, "Caerwyn Sabin-Rees\n");
    vga_printf("This OS began as a simple creative project\n");
    vga_printf("but has now expanded into the time-consuming addiction it is today!\n");
}

static void cmd_shutdown() {
    vga_printf("Requesting ACPI shutdown...\n");
    int result = acpi_shutdown();

    if (result == ACPI_REQUEST_ISSUED) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "ACPI shutdown request was ignored by the system\n");
        return;
    }

    switch (result) {
        case ACPI_ERR_RSDP:
            vga_printf_colour(LIGHT_RED, BLACK, "ACPI root pointer not found or unsupported\n");
            break;
        case ACPI_ERR_RSDT:
            vga_printf_colour(LIGHT_RED, BLACK, "ACPI root table is invalid\n");
            break;
        case ACPI_ERR_FADT:
            vga_printf_colour(LIGHT_RED, BLACK, "ACPI FADT not found or unsupported\n");
            break;
        case ACPI_ERR_DSDT:
            vga_printf_colour(LIGHT_RED, BLACK, "ACPI DSDT is invalid\n");
            break;
        case ACPI_ERR_S5:
            vga_printf_colour(LIGHT_RED, BLACK, "ACPI S5 sleep state not found\n");
            break;
        case ACPI_ERR_PM1:
            vga_printf_colour(LIGHT_RED, BLACK, "ACPI PM1 control register is unsupported\n");
            break;
        case ACPI_ERR_ENABLE:
            vga_printf_colour(LIGHT_RED, BLACK, "Could not enable ACPI mode\n");
            break;
        default:
            vga_printf_colour(LIGHT_RED, BLACK, "Unknown ACPI shutdown error: %d\n", result);
            break;
    }
}

static void cmd_reboot() {
    vga_printf("Requesting ACPI reboot...\n");
    int result = acpi_reboot();

    if (result != ACPI_REQUEST_ISSUED)
        vga_printf_colour(LIGHT_RED, BLACK,
                          "ACPI reset unavailable (error %d); trying keyboard controller\n",
                          result);

    for (uint32_t timeout = 1000000; timeout; timeout--) {
        if (!(inb(0x64) & 0x02)) {
            outb(0x64, 0xFE);
            break;
        }
    }

    for (;;)
        __asm__ volatile ("hlt");
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
    else if (strcmp(args[0], "rps")      == 0) cmd_rps();
    else if (strcmp(args[0], "taskdemo") == 0) cmd_taskdemo();
    else if (strcmp(args[0], "net")      == 0) cmd_net();
    else if (strcmp(args[0], "ping")     == 0) cmd_ping();

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
    vga_printf_colour(LIGHT_CYAN, BLACK, "   _            _     ___  ___  \n");
    vga_printf_colour(LIGHT_CYAN, BLACK, "  /_\\  ___ __ _(_)___/ _ \\/ __| \n");
    vga_printf_colour(LIGHT_CYAN, BLACK, " / _ \\/ -_) _` | (_-< (_) \\__ \\ \n");
    vga_printf_colour(LIGHT_CYAN, BLACK, "/_/ \\_\\___\\__, |_/__/\\___/|___/ \n");
    vga_printf_colour(LIGHT_CYAN, BLACK, "          |___/                 \n");      
    vga_printf("\n");
    vga_printf_colour(YELLOW,     BLACK, "  Welcome to AegisOS!\n");
    vga_printf_colour(LIGHT_GREY, BLACK, "  Type 'help' for a list of commands.\n\n");
    print_prompt();
}

static void process_key(char c) {
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

void shell_handle_key(char c) {
    uint32_t next = (key_write_index + 1) % KEY_QUEUE_SIZE;
    if (next == key_read_index)
        return;

    key_queue[key_write_index] = c;
    key_write_index = next;
}

void shell_process_input(void) {
    while (key_read_index != key_write_index) {
        char c = key_queue[key_read_index];
        key_read_index = (key_read_index + 1) % KEY_QUEUE_SIZE;
        process_key(c);
    }
}
