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
#define HTTP_RESPONSE_CAPACITY (FS_MAX_SIZE + 4096)
#define HTTP_DISPLAY_CAPACITY 1023
#define HTTP_PATH_MAX_LENGTH 200
// ============================================================
// State
// ============================================================

static multiboot_info_t *mbi_ptr      = 0;
static char              buf[BUFFER_SIZE];
static uint32_t          buf_length;
static uint32_t          buf_cursor;
static uint32_t          rendered_length;
static int               input_origin_row;
static int               input_origin_column;
static volatile uint32_t key_read_index;
static volatile uint32_t key_write_index;
static uint16_t key_queue[KEY_QUEUE_SIZE];

static char history[HISTORY_SIZE][BUFFER_SIZE];
static char history_draft[BUFFER_SIZE];
static uint32_t history_draft_length;
static int  history_count             = 0;
static int  history_index             = -1;
static int  history_browsing;

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
    "  tcp <IPv4|hostname> <port> - test and close a TCP connection",
    "  http <IPv4|hostname> [path] - fetch an HTTP path",
    "  http <IPv4|hostname> <path> <file> - download an HTTP body",
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
    input_origin_row = vga_get_row();
    input_origin_column = vga_get_column();
    rendered_length = 0;
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
    history_browsing = 0;
}

static void set_input_cursor(void) {
    uint32_t position = (uint32_t)input_origin_column + buf_cursor;
    int row = input_origin_row + position / VGA_TEXT_WIDTH;
    int column = position % VGA_TEXT_WIDTH;
    vga_set_cursor_position(row, column);
}

static void redraw_input_line(void) {
    uint32_t output_length = buf_length > rendered_length
                           ? buf_length : rendered_length;
    vga_set_cursor_position(input_origin_row, input_origin_column);

    for (uint32_t i = 0; i < buf_length; i++)
        vga_putchar(buf[i]);
    for (uint32_t i = buf_length; i < output_length; i++)
        vga_putchar(' ');

    uint32_t end_position = (uint32_t)input_origin_column + output_length;
    int end_row = input_origin_row + end_position / VGA_TEXT_WIDTH;
    if (end_row >= VGA_TEXT_HEIGHT) {
        int scroll_rows = end_row - VGA_TEXT_HEIGHT + 1;
        input_origin_row -= scroll_rows;
        if (input_origin_row < 0)
            input_origin_row = 0;
    }

    rendered_length = buf_length;
    set_input_cursor();
}

static void load_history_line(const char *line) {
    buf_length = strlen(line);
    if (buf_length >= BUFFER_SIZE)
        buf_length = BUFFER_SIZE - 1;
    for (uint32_t i = 0; i < buf_length; i++)
        buf[i] = line[i];
    buf[buf_length] = '\0';
    buf_cursor = buf_length;
    redraw_input_line();
}

static void navigate_history(int direction) {
    if (history_count == 0)
        return;

    if (!history_browsing) {
        if (direction > 0)
            return;
        for (uint32_t i = 0; i < buf_length; i++)
            history_draft[i] = buf[i];
        history_draft[buf_length] = '\0';
        history_draft_length = buf_length;
        history_index = history_count;
        history_browsing = 1;
    }

    if (direction < 0) {
        if (history_index > 0)
            history_index--;
        load_history_line(history[history_index]);
        return;
    }

    if (history_index < history_count - 1) {
        history_index++;
        load_history_line(history[history_index]);
    } else {
        history_index = history_count;
        history_browsing = 0;
        buf_length = history_draft_length;
        for (uint32_t i = 0; i < buf_length; i++)
            buf[i] = history_draft[i];
        buf[buf_length] = '\0';
        buf_cursor = buf_length;
        redraw_input_line();
    }
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

static int parse_tcp_port(const char *text, uint16_t *port) {
    if (!text || !*text || !port)
        return -1;

    uint32_t value = 0;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9')
            return -1;
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > (65535 - digit) / 10)
            return -1;
        value = value * 10 + digit;
    }

    if (value == 0)
        return -1;
    *port = (uint16_t)value;
    return 0;
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

static void cmd_tcp() {
    if (argc != 3) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "Usage: tcp <IPv4|hostname> <port>\n");
        return;
    }

    uint16_t port;
    if (parse_tcp_port(args[2], &port) != 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "tcp: invalid port: %s\n", args[2]);
        return;
    }

    net_interface_info_t info;
    net_get_interface(&info);
    if (!info.available) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "tcp: no supported network interface (QEMU e1000)\n");
        return;
    }

    uint32_t destination;
    if (net_parse_ipv4(args[1], &destination) != 0 &&
        net_resolve_ipv4(args[1], &destination) != 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "tcp: could not resolve host: %s\n", args[1]);
        return;
    }

    vga_printf("TCP connect to %s (", args[1]);
    print_ipv4(destination);
    vga_printf("):%u\n", port);

    int status = net_tcp_connect_close(destination, port);
    if (status == 0) {
        vga_printf("Connection established and closed cleanly.\n");
    } else if (status == -2) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "tcp: could not reach destination (ARP timeout)\n");
    } else if (status == -3) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "tcp: connection refused or packet send failed\n");
    } else if (status == -4) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "tcp: connection timed out\n");
    } else if (status == -5) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "tcp: peer did not complete a clean close\n");
    } else {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "tcp: connection failed (error %d)\n", status);
    }
}

static int http_response_body_offset(const char *response, uint32_t length) {
    for (uint32_t i = 0; i + 3 < length; i++) {
        if (response[i] == '\r' && response[i + 1] == '\n' &&
            response[i + 2] == '\r' && response[i + 3] == '\n')
            return i + 4;
    }
    return -1;
}

static int http_response_status(const char *response, uint32_t length,
                                uint16_t *status_code) {
    if (length < 12 || response[0] != 'H' || response[1] != 'T' ||
        response[2] != 'T' || response[3] != 'P' || response[4] != '/')
        return -1;

    uint32_t index = 5;
    while (index < length && response[index] != ' ')
        index++;
    while (index < length && response[index] == ' ')
        index++;
    if (index + 3 > length ||
        response[index] < '0' || response[index] > '9' ||
        response[index + 1] < '0' || response[index + 1] > '9' ||
        response[index + 2] < '0' || response[index + 2] > '9')
        return -1;

    *status_code = (uint16_t)((response[index] - '0') * 100 +
                              (response[index + 1] - '0') * 10 +
                              (response[index + 2] - '0'));
    return 0;
}

static void http_print_text(const char *text, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        char c = text[i];
        if (c == '\r')
            continue;
        if (c == '\n' || c == '\t' || (c >= 32 && c <= 126))
            vga_putchar(c);
        else
            vga_putchar('.');
    }
    if (length && text[length - 1] != '\n')
        vga_putchar('\n');
}

static char http_lowercase(char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int http_response_is_chunked(const char *response, uint32_t body_offset) {
    uint32_t line = 0;
    while (line + 1 < body_offset) {
        uint32_t end = line;
        while (end + 1 < body_offset &&
               !(response[end] == '\r' && response[end + 1] == '\n'))
            end++;
        if (end == line)
            break;

        static const char field[] = "transfer-encoding:";
        uint32_t field_length = sizeof(field) - 1;
        if (end - line >= field_length) {
            uint32_t i = 0;
            while (i < field_length &&
                   http_lowercase(response[line + i]) == field[i])
                i++;
            if (i == field_length) {
                for (uint32_t value = line + field_length; value < end; value++) {
                    static const char chunked[] = "chunked";
                    uint32_t j = 0;
                    while (j < sizeof(chunked) - 1 &&
                           value + j < end &&
                           http_lowercase(response[value + j]) == chunked[j])
                        j++;
                    if (j == sizeof(chunked) - 1)
                        return 1;
                }
            }
        }
        line = end + 2;
    }
    return 0;
}

static int http_decode_chunked(char *response, uint32_t body_offset,
                               uint32_t response_length,
                               uint32_t *decoded_length) {
    uint32_t input = body_offset;
    uint32_t output = 0;

    while (input < response_length) {
        uint32_t chunk_size = 0;
        uint32_t digits = 0;
        while (input < response_length && response[input] != '\r' &&
               response[input] != ';') {
            char c = response[input++];
            uint32_t digit;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (http_lowercase(c) >= 'a' && http_lowercase(c) <= 'f')
                digit = http_lowercase(c) - 'a' + 10;
            else
                return -1;
            if (chunk_size > (FS_MAX_SIZE - digit) / 16)
                return -2;
            chunk_size = chunk_size * 16 + digit;
            digits++;
        }
        if (!digits)
            return -1;

        while (input < response_length && response[input] == ';') {
            while (input < response_length && response[input] != '\r')
                input++;
        }
        if (input + 1 >= response_length ||
            response[input] != '\r' || response[input + 1] != '\n')
            return -1;
        input += 2;

        if (chunk_size == 0) {
            while (input + 1 < response_length) {
                if (response[input] == '\r' && response[input + 1] == '\n') {
                    *decoded_length = output;
                    return 0;
                }
                uint32_t trailer_end = input;
                while (trailer_end + 1 < response_length &&
                       !(response[trailer_end] == '\r' &&
                         response[trailer_end + 1] == '\n'))
                    trailer_end++;
                if (trailer_end + 1 >= response_length)
                    return -1;
                input = trailer_end + 2;
            }
            return -1;
        }

        if (chunk_size > FS_MAX_SIZE - output ||
            chunk_size > response_length - input ||
            input + chunk_size + 2 > response_length ||
            response[input + chunk_size] != '\r' ||
            response[input + chunk_size + 1] != '\n')
            return chunk_size > FS_MAX_SIZE - output ? -2 : -1;

        for (uint32_t i = 0; i < chunk_size; i++)
            response[output + i] = response[input + i];
        output += chunk_size;
        input += chunk_size + 2;
    }

    return -1;
}

static int http_valid_filename(const char *name) {
    if (!name || !*name)
        return 0;
    uint32_t length = 0;
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return 0;
        if (++length >= FS_MAX_NAME)
            return 0;
    }
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static void cmd_http() {
    if (argc < 2 || argc > 4) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "Usage: http <IPv4|hostname> [path] [file]\n");
        return;
    }

    const char *path = argc >= 3 ? args[2] : "/";
    uint32_t path_length = 0;
    while (path[path_length]) {
        path_length++;
        if (path_length > HTTP_PATH_MAX_LENGTH) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "http: path is too long (maximum %u characters)\n",
                              HTTP_PATH_MAX_LENGTH);
            return;
        }
    }
    if (path[0] != '/') {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "http: path must begin with '/'\n");
        return;
    }
    if (argc == 4) {
        if (!http_valid_filename(args[3])) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "http: invalid filename (use up to %u letters, digits, '.', '_' or '-')\n",
                              FS_MAX_NAME - 1);
            return;
        }
        if (fs_exists(args[3])) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "http: file already exists: %s (remove it first)\n",
                              args[3]);
            return;
        }
    }

    net_interface_info_t info;
    net_get_interface(&info);
    if (!info.available) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "http: no supported network interface (QEMU e1000)\n");
        return;
    }

    uint32_t destination;
    if (net_parse_ipv4(args[1], &destination) != 0 &&
        net_resolve_ipv4(args[1], &destination) != 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "http: could not resolve host: %s\n", args[1]);
        return;
    }

    static char response[HTTP_RESPONSE_CAPACITY];
    uint32_t response_length = 0;
    uint8_t truncated = 0;
    vga_printf("GET http://%s%s (", args[1], path);
    print_ipv4(destination);
    vga_printf(")\n");

    int status = net_http_get(destination, args[1], path, response,
                              sizeof(response), &response_length, &truncated);
    uint32_t body_offset = 0;
    uint32_t body_length = 0;
    char *body = 0;
    int chunked = 0;
    uint16_t response_status = 0;
    int has_response_status = 0;
    if (response_length) {
        int parsed_body_offset =
            http_response_body_offset(response, response_length);
        has_response_status =
            http_response_status(response, response_length,
                                 &response_status) == 0;
        if (has_response_status)
            vga_printf("HTTP status: %u\n", response_status);
        else
            vga_printf("HTTP status: unavailable\n");

        if (parsed_body_offset >= 0) {
            body_offset = (uint32_t)parsed_body_offset;
            body_length = response_length - body_offset;
            chunked = http_response_is_chunked(response, body_offset);
            body = response + body_offset;
            if (chunked) {
                int decode_status = http_decode_chunked(response, body_offset,
                                                        response_length,
                                                        &body_length);
                if (decode_status == 0)
                    body = response;
                else {
                    body = 0;
                    vga_printf_colour(LIGHT_RED, BLACK,
                                      "http: malformed or oversized chunked response\n");
                }
            }

            if (body) {
                uint32_t display_length = body_length;
                if (display_length > HTTP_DISPLAY_CAPACITY)
                    display_length = HTTP_DISPLAY_CAPACITY;
                vga_printf("--- response body%s ---\n",
                           truncated || display_length < body_length
                               ? " (display limited)" : "");
                http_print_text(body, display_length);
            }
        } else {
            vga_printf_colour(YELLOW, BLACK,
                              "HTTP headers incomplete; showing received response\n");
            uint32_t display_length = response_length;
            if (display_length > HTTP_DISPLAY_CAPACITY)
                display_length = HTTP_DISPLAY_CAPACITY;
            http_print_text(response, display_length);
        }
    }

    if (status == 0) {
        vga_printf("HTTP connection closed cleanly.\n");
        if (argc == 4) {
            if (!body || body_offset == 0 || !has_response_status) {
                vga_printf_colour(LIGHT_RED, BLACK,
                                  "http: response body is incomplete or invalid; nothing saved\n");
            } else if (response_status < 200 || response_status >= 300) {
                vga_printf_colour(LIGHT_RED, BLACK,
                                  "http: not saving unsuccessful HTTP status %u\n",
                                  response_status);
            } else if (truncated) {
                vga_printf_colour(LIGHT_RED, BLACK,
                                  "http: response exceeded the download limit; nothing saved\n");
            } else if (body_length > FS_MAX_SIZE) {
                vga_printf_colour(LIGHT_RED, BLACK,
                                  "http: response body exceeds the filesystem limit (%u bytes)\n",
                                  FS_MAX_SIZE);
            } else if (fs_write(args[3], (const uint8_t *)body,
                                body_length) != 0) {
                vga_printf_colour(LIGHT_RED, BLACK,
                                  "http: failed to save %s\n", args[3]);
            } else {
                vga_printf("Saved %u bytes to %s\n", body_length, args[3]);
            }
        }
    } else if (status == -2) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "http: could not reach destination (ARP timeout)\n");
    } else if (status == -3) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "http: connection refused or packet send failed\n");
    } else if (status == -4) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "http: connection timed out\n");
    } else if (status == -5) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "http: response or connection close timed out\n");
    } else {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "http: request failed (error %d)\n", status);
    }
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
    else if (strcmp(args[0], "tcp")      == 0) cmd_tcp();
    else if (strcmp(args[0], "http")     == 0) cmd_http();

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

static void process_key(uint16_t key) {
    if (key == '\n') {
        buf[buf_length] = '\0';
        buf_cursor = buf_length;
        redraw_input_line();
        vga_putchar('\n');
        if (buf_length > 0) {
            history_add(buf);
            execute(buf);
        }
        buf_length = 0;
        buf_cursor = 0;
        rendered_length = 0;
        buf[0] = '\0';
        history_index = history_count;
        history_browsing = 0;
        print_prompt();
    } else if (key == '\b') {
        if (buf_cursor > 0) {
            for (uint32_t i = buf_cursor - 1; i < buf_length - 1; i++)
                buf[i] = buf[i + 1];
            buf_length--;
            buf_cursor--;
            buf[buf_length] = '\0';
            redraw_input_line();
        }
    } else if (key == KEY_DELETE) {
        if (buf_cursor < buf_length) {
            for (uint32_t i = buf_cursor; i < buf_length - 1; i++)
                buf[i] = buf[i + 1];
            buf_length--;
            buf[buf_length] = '\0';
            redraw_input_line();
        }
    } else if (key == ARROW_UP) {
        navigate_history(-1);
    } else if (key == ARROW_DOWN) {
        navigate_history(1);
    } else if (key == ARROW_LEFT) {
        if (buf_cursor > 0) {
            buf_cursor--;
            set_input_cursor();
        }
    } else if (key == ARROW_RIGHT) {
        if (buf_cursor < buf_length) {
            buf_cursor++;
            set_input_cursor();
        }
    } else if (key == KEY_HOME) {
        buf_cursor = 0;
        set_input_cursor();
    } else if (key == KEY_END) {
        buf_cursor = buf_length;
        set_input_cursor();
    } else if (key >= 32 && key <= 126 && buf_length < BUFFER_SIZE - 1) {
        for (uint32_t i = buf_length; i > buf_cursor; i--)
            buf[i] = buf[i - 1];
        buf[buf_cursor++] = (char)key;
        buf_length++;
        buf[buf_length] = '\0';
        redraw_input_line();
    }
}

void shell_handle_key(uint16_t key) {
    uint32_t next = (key_write_index + 1) % KEY_QUEUE_SIZE;
    if (next == key_read_index)
        return;

    key_queue[key_write_index] = key;
    key_write_index = next;
}

void shell_process_input(void) {
    while (key_read_index != key_write_index) {
        uint16_t key = key_queue[key_read_index];
        key_read_index = (key_read_index + 1) % KEY_QUEUE_SIZE;
        process_key(key);
    }
}
