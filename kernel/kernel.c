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

extern uint32_t stack_top;
void (*exec_return_callback)() = 0;

void kernel_main(multiboot_info_t *mbi) {
    serial_init();
    vga_init();
    gdt_init();
    tss_set_kernel_stack((uint32_t)&stack_top);
    idt_init();
    pic_init();
    irq_init();
    pit_init(1000);
    rtc_init();
    keyboard_init();
    pmm_init(mbi);
    paging_init();
    heap_init();
    __asm__ volatile ("sti");
    pit_sleep(500);   
    ata_init();
    pit_sleep(500);   
    fs_init();
    syscall_init();
    vga_clear();
    shell_init(mbi);
    while(1);
}