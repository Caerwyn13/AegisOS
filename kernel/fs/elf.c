#include "elf.h"
#include "vga.h"
#include "aegisfs.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"

// ============================================================
// Validation
// ============================================================

int elf_check_file(elf32_ehdr *hdr) {
    if (!hdr) return 0;
    if (hdr->e_ident[EI_MAG0] != ELFMAG0) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: EI_MAG0 incorrect\n");
        return 0;
    }
    if (hdr->e_ident[EI_MAG1] != ELFMAG1) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: EI_MAG1 incorrect\n");
        return 0;
    }
    if (hdr->e_ident[EI_MAG2] != ELFMAG2) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: EI_MAG2 incorrect\n");
        return 0;
    }
    if (hdr->e_ident[EI_MAG3] != ELFMAG3) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: EI_MAG3 incorrect\n");
        return 0;
    }
    return 1;
}

int elf_check_supported(elf32_ehdr *hdr) {
    if (!elf_check_file(hdr)) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: invalid file\n");
        return 0;
    }
    if (hdr->e_ident[EI_CLASS] != ELFCLASS32) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: unsupported class\n");
        return 0;
    }
    if (hdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: unsupported byte order\n");
        return 0;
    }
    if (hdr->e_ident[EI_VERSION] != EV_CURRENT) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: unsupported version\n");
        return 0;
    }
    if (hdr->e_machine != EM_386) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: unsupported target\n");
        return 0;
    }
    if (hdr->e_type != ET_REL && hdr->e_type != ET_EXEC) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: unsupported type\n");
        return 0;
    }
    return 1;
}

// ============================================================
// Loader
// ============================================================

int elf_load(const char *name, uint32_t *entry) {
    uint8_t *buf = (uint8_t *)kmalloc(FS_MAX_SIZE);
    if (!buf) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: out of memory\n");
        return -1;
    }

    uint32_t size = 0;
    if (fs_read(name, buf, &size) < 0) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: file not found: %s\n", name);
        kfree(buf);
        return -1;
    }

    elf32_ehdr *hdr = (elf32_ehdr *)buf;
    if (!elf_check_supported(hdr)) {
        kfree(buf);
        return -1;
    }

    int i;
    for (i = 0; i < hdr->e_phnum; i++) {
        elf32_phdr *ph = (elf32_phdr *)(buf + hdr->e_phoff + i * hdr->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0)      continue;

        uint32_t vaddr     = ph->p_vaddr & ~0xFFF;
        uint32_t vaddr_end = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFF;
        uint32_t v;
        for (v = vaddr; v < vaddr_end; v += 0x1000) {
            uint32_t phys = (uint32_t)pmm_alloc();
            paging_map(v, phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
        }

        memcpy((void *)ph->p_vaddr, buf + ph->p_offset, ph->p_filesz);

        if (ph->p_memsz > ph->p_filesz)
            memset((void *)(ph->p_vaddr + ph->p_filesz), 0,
                   ph->p_memsz - ph->p_filesz);
    }

    *entry = hdr->e_entry;
    kfree(buf);
    return 0;
}