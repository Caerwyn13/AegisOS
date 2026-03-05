// kernel/gdt.c
// Contains all functions related to the Global Descriptor Table
#include "gdt.h"

#define GDT_ENTRIES 3

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

extern void gdt_flush(uint32_t);

static void set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
	gdt[i].base_low  = base & 0xFFFF;
	gdt[i].base_mid  = (base >> 16) & 0xFF;
	gdt[i].base_high = (base >> 24) & 0xFF;
	gdt[i].limit_low   = limit & 0xFFFF;
	gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
	gdt[i].access      = access;
}

void gdt_init() {
	gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
	gdt_ptr.base  = (uint32_t)&gdt;

	set_entry(0, 0, 0,          0x00, 0x00); // NULL
	set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // CODE
	set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // DATA
	
	gdt_flush((uint32_t)&gdt_ptr);
}

