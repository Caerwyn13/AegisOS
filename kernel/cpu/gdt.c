#include "gdt.h"
#include "string.h"

#define GDT_ENTRIES 6

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;
static tss_entry_t tss;

extern void gdt_flush(uint32_t);
extern void tss_flush();

static void set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
	gdt[i].base_low  = base & 0xFFFF;
	gdt[i].base_mid  = (base >> 16) & 0xFF;
	gdt[i].base_high = (base >> 24) & 0xFF;
	gdt[i].limit_low   = limit & 0xFFFF;
	gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
	gdt[i].access      = access;
}

static void tss_init(uint32_t idx, uint32_t ss0, uint32_t esp0) {
	uint32_t base  = (uint32_t)&tss;
	uint32_t limit = sizeof(tss_entry_t) - 1;

	// Zero out the entire TSS
	memset(&tss, 0, sizeof(tss_entry_t));

	set_entry(idx, base, limit, 0x89, 0x00);

	tss.ss0 	   = ss0;
	tss.esp0 	   = esp0;
	tss.iomap_base = sizeof(tss_entry_t);
}

void tss_set_kernel_stack(uint32_t stack) {
	tss.esp0 = stack;
}

void gdt_init() {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    set_entry(0, 0, 0,          0x00, 0x00);
    set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    tss_init(5, 0x10, 0);

    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
}