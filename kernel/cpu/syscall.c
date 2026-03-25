#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "isr.h"
#include "rtc.h"
#include "aegisfs.h"
#include "string.h"
#include "usermode.h"
#include "paging.h"
#include "memmap.h"

int process_exited = 0;

// extern kernel stack from paging.c
extern uint8_t kernel_stack[KERNEL_STACK_SIZE];
extern uint32_t kernel_stack_top;

// User memory range
#define USER_START 0x00400000
#define USER_END   0x08000000

static void sys_exit(int code) {
    // Print exit info
    vga_printf("Process exited with code %d\n", code);

    // Switch back to kernel page directory
    pde_t* kernel_dir = paging_get_kernel_directory();
    paging_switch_directory(kernel_dir);

    // Switch to kernel stack (must be mapped in kernel page directory)
    asm volatile("mov %0, %%esp" :: "r"(kernel_stack_top));

    // Unmap all user-space pages
    paging_unmap_range(USER_START, USER_END);

    // Mark process as exited
    process_exited = 1;

    // Return to shell main loop or halt if shell is not implemented
    // For now we just loop safely
    while (1) {
        asm volatile("hlt");
    }
}

static void sys_print(const char *str) {
    if (!str) return;
    vga_print(str);
}

static int sys_read(const char *name, uint8_t *buf, uint32_t *size) {
    return fs_read(name, buf, size);
}

static int sys_write(const char *name, const uint8_t *buf, uint32_t size) {
    return fs_write(name, buf, size);
}

static int sys_open(const char *name) {
    if (fs_exists(name)) return 0;
    return fs_create(name);
}

static void sys_gettime(rtc_time_t *t) {
    if (!t) return;
    *t = rtc_get_time();
}

void syscall_handler(registers_t *regs) {
    uint32_t num  = regs->eax;
    uint32_t arg1 = regs->ebx;
    uint32_t arg2 = regs->ecx;
    uint32_t arg3 = regs->edx;

    switch (num) {
        case SYS_EXIT:
            sys_exit((int)arg1);
            break;
        case SYS_PRINT:
            sys_print((const char *)arg1);
            break;
        case SYS_READ:
            sys_read((const char *)arg1, (uint8_t *)arg2, (uint32_t *)arg3);
            break;
        case SYS_WRITE:
            sys_write((const char *)arg1, (const uint8_t *)arg2, arg3);
            break;
        case SYS_OPEN:
            regs->eax = sys_open((const char *)arg1);
            break;
        case SYS_GETTIME:
            sys_gettime((rtc_time_t *)arg1);
            break;
        default:
            vga_printf_colour(LIGHT_RED, BLACK, "Unknown syscall: %u\n", num);
            break;
    }
}

void syscall_init() {
    idt_set_entry(0x80, (uint32_t)isr128, 0x08, 0xEE);
    vga_printf("Syscalls initialised\n");
}