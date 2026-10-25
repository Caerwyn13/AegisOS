#include "elf.h"
#include "vga.h"
#include "aegisfs.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"

#define USER_ELF_MIN_VADDR 0x00400000
#define USER_ELF_MAX_SEGMENTS 16
#define USER_ELF_STACK_BOTTOM (USER_STACK_TOP - USER_STACK_SIZE)

typedef struct {
    uint32_t start;
    uint32_t end;
} elf_mapped_range_t;

static elf_mapped_range_t loaded_segments[USER_ELF_MAX_SEGMENTS];
static uint32_t loaded_segment_count;
static int stack_mapping_attempted;

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
           hdr->e_version == EV_CURRENT &&
           hdr->e_machine == EM_386 &&
           hdr->e_type == ET_EXEC;
}

void elf_unload(void) {
    for (uint32_t i = 0; i < loaded_segment_count; i++)
        paging_unmap_range(loaded_segments[i].start, loaded_segments[i].end);
    if (stack_mapping_attempted)
        paging_unmap_range(USER_ELF_STACK_BOTTOM, USER_STACK_TOP);
    memset(loaded_segments, 0, sizeof(loaded_segments));
    loaded_segment_count = 0;
    stack_mapping_attempted = 0;
}

static int validate_header(const elf32_ehdr *hdr, uint32_t size) {
    if (size < sizeof(*hdr) || hdr->e_ehsize != sizeof(*hdr) ||
        hdr->e_phentsize != sizeof(elf32_phdr) || !hdr->e_phnum)
        return -1;
    if (hdr->e_phoff > size ||
        hdr->e_phnum > (size - hdr->e_phoff) / hdr->e_phentsize)
        return -1;
    return 0;
}

static const elf32_phdr *program_header(const uint8_t *buffer,
                                         const elf32_ehdr *header,
                                         uint32_t index) {
    return (const elf32_phdr *)(buffer + header->e_phoff +
                                index * header->e_phentsize);
}

static int validate_load_segment(const elf32_phdr *segment, uint32_t size,
                                 uint32_t *map_start, uint32_t *map_end) {
    if (segment->p_filesz > segment->p_memsz ||
        segment->p_offset > size ||
        segment->p_filesz > size - segment->p_offset ||
        segment->p_memsz > 0xFFFFFFFFu - segment->p_vaddr)
        return -1;
    if (!segment->p_memsz) {
        *map_start = 0;
        *map_end = 0;
        return 0;
    }

    uint32_t segment_end = segment->p_vaddr + segment->p_memsz;
    if (segment_end <= USER_ELF_MIN_VADDR) {
        *map_start = 0;
        *map_end = 0;
        return 0;
    }
    if (segment->p_vaddr < USER_ELF_MIN_VADDR || segment_end > HEAP_START)
        return -1;

    *map_start = segment->p_vaddr & ~(PAGE_SIZE - 1);
    *map_end = (segment_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (*map_end < segment_end)
        return -1;
    return 0;
}

static int ranges_overlap(uint32_t start, uint32_t end,
                          uint32_t other_start, uint32_t other_end) {
    return start < other_end && other_start < end;
}

static int load_segment(const elf32_phdr *segment, uint8_t *buffer,
                        uint32_t map_start, uint32_t map_end) {
    int writable = (segment->p_flags & PF_W) != 0;
    if (paging_map_user_range(map_start, map_end, writable) != 0)
        return -1;

    uint32_t file_end = segment->p_vaddr + segment->p_filesz;
    for (uint32_t page = map_start; page < map_end; page += PAGE_SIZE) {
        if (!paging_get_phys(page))
            return -1;
        memset((void *)page, 0, PAGE_SIZE);

        uint32_t copy_start = page > segment->p_vaddr
            ? page : segment->p_vaddr;
        uint32_t copy_end = page + PAGE_SIZE < file_end
            ? page + PAGE_SIZE : file_end;
        if (copy_end > copy_start) {
            uint32_t file_offset = segment->p_offset +
                                   (copy_start - segment->p_vaddr);
            memcpy((void *)copy_start, buffer + file_offset,
                   copy_end - copy_start);
        }
    }
    return 0;
}

int elf_load(const char *name, uint32_t *entry_out) {
    if (!name || !entry_out) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: invalid loader arguments\n");
        return -1;
    }

    elf_unload();
    uint8_t *buffer = (uint8_t *)kmalloc(FS_MAX_SIZE);
    if (!buffer) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: out of memory for buffer\n");
        return -1;
    }

    uint32_t size = 0;
    if (fs_read(name, buffer, &size) < 0) {
        vga_printf_colour(LIGHT_RED, BLACK, "ELF: file not found: %s\n", name);
        kfree(buffer);
        return -1;
    }

    elf32_ehdr *header = (elf32_ehdr *)buffer;
    if (size > FS_MAX_SIZE || size < sizeof(*header) ||
        !elf_check_supported(header) || validate_header(header, size) != 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "ELF: invalid or unsupported executable\n");
        kfree(buffer);
        return -1;
    }

    int entry_executable = 0;
    for (uint32_t i = 0; i < header->e_phnum; i++) {
        const elf32_phdr *segment = program_header(buffer, header, i);
        if (segment->p_type != PT_LOAD)
            continue;

        uint32_t map_start;
        uint32_t map_end;
        if (validate_load_segment(segment, size, &map_start, &map_end) != 0) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "ELF: invalid load segment\n");
            goto fail;
        }
        if (!segment->p_memsz || map_start == map_end)
            continue;
        if (loaded_segment_count >= USER_ELF_MAX_SEGMENTS) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "ELF: too many load segments\n");
            goto fail;
        }
        for (uint32_t j = 0; j < loaded_segment_count; j++) {
            if (ranges_overlap(map_start, map_end,
                               loaded_segments[j].start,
                               loaded_segments[j].end)) {
                vga_printf_colour(LIGHT_RED, BLACK,
                                  "ELF: overlapping load segments\n");
                goto fail;
            }
        }

        loaded_segments[loaded_segment_count].start = map_start;
        loaded_segments[loaded_segment_count].end = map_end;
        loaded_segment_count++;

        if ((segment->p_flags & PF_X) &&
            header->e_entry >= segment->p_vaddr &&
            header->e_entry < segment->p_vaddr + segment->p_memsz)
            entry_executable = 1;

        if (load_segment(segment, buffer, map_start, map_end) != 0) {
            vga_printf_colour(LIGHT_RED, BLACK,
                              "ELF: failed to map load segment\n");
            goto fail;
        }
    }

    if (!entry_executable) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "ELF: entry point is outside executable segments\n");
        goto fail;
    }

    stack_mapping_attempted = 1;
    if (paging_map_user_range(USER_ELF_STACK_BOTTOM, USER_STACK_TOP, 1) != 0) {
        vga_printf_colour(LIGHT_RED, BLACK,
                          "ELF: failed to allocate user stack\n");
        goto fail;
    }

    *entry_out = header->e_entry;
    kfree(buffer);
    return 0;

fail:
    kfree(buffer);
    elf_unload();
    return -1;
}
