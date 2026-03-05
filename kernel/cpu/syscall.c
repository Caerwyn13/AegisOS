#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "isr.h"
#include "rtc.h"
#include "aegisfs.h"
#include "string.h"

// syscall handler - called from isr_common when int 0x80 fires
// eax = syscall number
// ebx = arg1, ecx = arg2, edx = arg3

static void sys_exit(int code) {
    vga_printf("Process exited with code %d\n", code);
    // for now just hang, later we'll return to scheduler
    for(;;);
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
    idt_set_entry(0x80, (uint32_t)isr128, 0x08, 0x8E);
    vga_printf("Syscalls initialised\n");
}