#include "elf.h"
#include "vga.h"
#include "aegisfs.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"

#define USER_ELF_MIN_VADDR 0x00400000

// ============================================================
// Validation
// ============================================================
int elf_check_file(elf32_ehdr *hdr) {
    if (!hdr) return 0;
    return hdr->e_ident[EI_MAG0] == ELFMAG0 &&
           hdr->e_ident[EI_MAG1] == ELFMAG1 &&
           hdr->e_ident[EI_MAG2] == ELFMAG2 &&
           hdr->e_ident[EI_MAG3] == ELFMAG3;
}

int elf_check_supported(elf32_ehdr *hdr) {
    if (!elf_check_file(hdr)) return 0;
    return hdr->e_ident[EI_CLASS] == ELFCLASS32 &&
           hdr->e_ident[EI_DATA] == ELFDATA2LSB &&
           hdr->e_ident[EI_VERSION] == EV_CURRENT &&
           hdr->e_machine == EM_386 &&
           (hdr->e_type == ET_REL || hdr->e_type == ET_EXEC);
}

// ============================================================
// Loader
// ============================================================
int elf_load(const char *name, uint32_t *entry_out) {
    uint8_t *buf = (uint8_t*)kmalloc(FS_MAX_SIZE);
    if (!buf) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: out of memory for buffer\n");
        return -1;
    }

    uint32_t size = 0;
    if (fs_read(name, buf, &size) < 0) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: file not found: %s\n", name);
        kfree(buf);
        return -1;
    }

    elf32_ehdr *hdr = (elf32_ehdr*)buf;
    if (!elf_check_supported(hdr)) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: unsupported file\n");
        kfree(buf);
        return -1;
    }

    // =============================
    // Step 1: Map all PT_LOAD segments
    // =============================
    for (int i = 0; i < hdr->e_phnum; i++) {
        elf32_phdr *ph = (elf32_phdr*)(buf + hdr->e_phoff + i * hdr->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        uint32_t seg_start = ph->p_vaddr & ~0xFFF;
        uint32_t seg_end   = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFF;
        int writable = (ph->p_flags & 0x2) != 0;
        
        if (seg_end <= USER_ELF_MIN_VADDR) {
            continue;
        }

        if (seg_start < USER_ELF_MIN_VADDR) {
            seg_start = USER_ELF_MIN_VADDR;
        }

        if (paging_map_user_range(seg_start, seg_end, writable) != 0) {
            vga_printf_colour(LIGHT_RED, BLACK, "ELF: PMM out of memory mapping segment\n");
            kfree(buf);
            return -1;
        }

        // Copy initialized data
        for (uint32_t v = seg_start; v < seg_end; v += PAGE_SIZE) {
            if (!paging_get_phys(v)) {
                vga_printf_colour(LIGHT_RED, BLACK, "ELF: missing physical page at 0x%x\n", v);
                kfree(buf);
                return -1;
            }

            uint32_t page_file_start = 0;
            uint32_t page_file_end = PAGE_SIZE;

            if (v < ph->p_vaddr) page_file_start = ph->p_vaddr - v;
            if (v + PAGE_SIZE > ph->p_vaddr + ph->p_filesz)
                page_file_end = (ph->p_vaddr + ph->p_filesz > v) ? ph->p_filesz - (v - ph->p_vaddr) : 0;

            // Copy file data via the mapped virtual address, not the physical one.
            if (page_file_end > page_file_start) {
                uint32_t file_offset = ph->p_offset + (v - seg_start) + page_file_start;
                memcpy((void*)(v + page_file_start), buf + file_offset,
                       page_file_end - page_file_start);
            }

            // Zero BSS through the virtual mapping too.
            uint32_t bss_start = v + page_file_end;
            uint32_t bss_end = v + PAGE_SIZE;
            if (bss_end > bss_start) memset((void*)bss_start, 0, bss_end - bss_start);
        }
    }

    // =============================
    // Step 2: Allocate user stack
    // =============================
    uint32_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
    if (paging_map_user_range(stack_bottom, USER_STACK_TOP, 1) != 0) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: failed to allocate user stack\n");
        kfree(buf);
        return -1;
    }

    *entry_out = hdr->e_entry;
    kfree(buf);
    return 0;
}
