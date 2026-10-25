#include "types.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "pic.h"
#include "serial.h"
#include "keyboard.h"
#include "pmm.h"
#include "vga.h"
#include "shell.h"
#include "multiboot.h"
#include "pit.h"
#include "rtc.h"
#include "paging.h"
#include "ata.h"
#include "aegisfs.h"
#include "heap.h"
#include "syscall.h"
#include "task.h"
#include "net.h"

extern uint32_t stack_top;
extern uint32_t kernel_stack_top;
void (*exec_return_callback)() = 0;

static void boot_status(const char *message) {
    vga_printf_colour(LIGHT_GREEN, BLACK, "[  OK  ] ");
    vga_printf_colour(LIGHT_GREY, BLACK, "%s\n", message);
}

void kernel_main(multiboot_info_t *mbi) {
    serial_init();
    vga_init();
    vga_clear();
    vga_printf_colour(LIGHT_CYAN, BLACK, "AegisOS kernel boot\n");

    gdt_init();
    tss_set_kernel_stack((uint32_t)&stack_top);
    idt_init();
    boot_status("GDT and interrupt descriptor table loaded");
    pic_init();
    irq_init();
    pit_init(1000);
    boot_status("Interrupt controllers and system timer configured");
    rtc_init();
    boot_status("Real-time clock configured");
    keyboard_init();
    boot_status("Keyboard input ready");
    if (pmm_init(mbi) != 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "[FAILED] Physical memory initialization; system halted\n");
        for (;;)
            __asm__ volatile ("hlt");
    }
    paging_init();
    tss_set_kernel_stack(kernel_stack_top);
    heap_init();
    task_init();
    boot_status("Task scheduler initialized");
    __asm__ volatile ("sti");
#ifndef ACPI_TEST_NO_STORAGE
    pit_sleep(500);   
    ata_init();
    pit_sleep(500);   
    fs_init();
#else
    vga_printf_colour(YELLOW, BLACK,
                      "[  SKIP ] Storage disabled in ACPI test build\n");
#endif
    syscall_init();
    net_init();
    net_interface_info_t network;
    net_get_interface(&network);
    if (network.available)
        boot_status("e1000 network configured by DHCP");
    else
        vga_printf_colour(YELLOW, BLACK,
                          "[  WARN ] Network unavailable (e1000/DHCP)\n");
    vga_printf("\n");
    vga_clear();
    shell_init(mbi);
    while (1)
        shell_process_input();
}