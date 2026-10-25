#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "isr.h"
#include "rtc.h"
#include "aegisfs.h"
#include "heap.h"
#include "paging.h"
#include "string.h"

#define SYSCALL_MAX_STRING 4096

int process_exited = 0;

static int copy_user_string(uint32_t address, char *buffer, uint32_t capacity) {
    if (!address || !capacity)
        return -1;

    for (uint32_t i = 0; i < capacity; i++) {
        uint32_t current = address + i;
        if (current < address ||
            !paging_user_range_accessible(current, 1, 0))
            return -1;
        buffer[i] = ((const char *)address)[i];
        if (!buffer[i])
            return 0;
    }
    buffer[capacity - 1] = '\0';
    return -1;
}

static int user_string_is_valid(uint32_t address, uint32_t max_length) {
    if (!address)
        return 0;

    for (uint32_t i = 0; i < max_length; i++) {
        uint32_t current = address + i;
        if (current < address ||
            !paging_user_range_accessible(current, 1, 0))
            return 0;
        if (!((const char *)address)[i])
            return 1;
    }
    return 0;
}

static void sys_exit(int code) {
    vga_printf("Process exited with code %d\n", code);
    process_exited = 1;
}

static int sys_print(uint32_t address) {
    if (!user_string_is_valid(address, SYSCALL_MAX_STRING))
        return -1;
    vga_print((const char *)address);
    return 0;
}

static int sys_read(uint32_t name_address, uint32_t buffer_address,
                    uint32_t size_address) {
    char name[FS_MAX_NAME];
    if (copy_user_string(name_address, name, sizeof(name)) != 0 ||
        !paging_user_range_accessible(size_address, sizeof(uint32_t), 1))
        return -1;

    uint8_t *buffer = kmalloc(FS_MAX_SIZE);
    if (!buffer)
        return -1;

    uint32_t size = 0;
    int status = fs_read(name, buffer, &size);
    if (status == 0 &&
        (!paging_user_range_accessible(buffer_address, size, 1) ||
         !paging_user_range_accessible(size_address, sizeof(size), 1)))
        status = -1;

    if (status == 0) {
        if (size)
            memcpy((void *)buffer_address, buffer, size);
        *(uint32_t *)size_address = size;
    }
    kfree(buffer);
    return status;
}

static int sys_write(uint32_t name_address, uint32_t buffer_address,
                     uint32_t size) {
    char name[FS_MAX_NAME];
    if (copy_user_string(name_address, name, sizeof(name)) != 0 ||
        size > FS_MAX_SIZE ||
        !paging_user_range_accessible(buffer_address, size, 0))
        return -1;
    return fs_write(name, (const uint8_t *)buffer_address, size);
}

static int sys_open(uint32_t name_address) {
    char name[FS_MAX_NAME];
    if (copy_user_string(name_address, name, sizeof(name)) != 0)
        return -1;
    if (fs_exists(name)) return 0;
    return fs_create(name);
}

static int sys_gettime(uint32_t address) {
    if (!paging_user_range_accessible(address, sizeof(rtc_time_t), 1))
        return -1;
    rtc_time_t time;
    if (rtc_get_time(&time) != 0)
        return -1;
    *(rtc_time_t *)address = time;
    return 0;
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
            regs->eax = sys_print(arg1);
            break;
        case SYS_READ:
            regs->eax = sys_read(arg1, arg2, arg3);
            break;
        case SYS_WRITE:
            regs->eax = sys_write(arg1, arg2, arg3);
            break;
        case SYS_OPEN:
            regs->eax = sys_open(arg1);
            break;
        case SYS_GETTIME:
            regs->eax = sys_gettime(arg1);
            break;
        case SYS_CLOSE:
            regs->eax = 0;
            break;
        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}

void syscall_init() {
    idt_set_entry(0x80, (uint32_t)isr128, 0x08, 0xEE);
    vga_printf("Syscalls initialised\n");
}