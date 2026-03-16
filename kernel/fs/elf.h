#ifndef ELF_H
#define ELF_H

#include "types.h"

// ============================================================
// ELF identification
// ============================================================

#define ELF_NIDENT 16

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFDATA2LSB 1  // little endian
#define ELFCLASS32  1  // 32-bit
#define EV_CURRENT  1  // current version
#define EM_386      3  // x86

// ============================================================
// Types
// ============================================================

typedef uint16_t elf32_half;
typedef uint32_t elf32_off;
typedef uint32_t elf32_addr;
typedef uint32_t elf32_word;
typedef int32_t  elf32_sword;

// ============================================================
// ELF header
// ============================================================

typedef struct {
    uint8_t    e_ident[ELF_NIDENT];
    elf32_half e_type;
    elf32_half e_machine;
    elf32_word e_version;
    elf32_addr e_entry;
    elf32_off  e_phoff;
    elf32_off  e_shoff;
    elf32_word e_flags;
    elf32_half e_ehsize;
    elf32_half e_phentsize;
    elf32_half e_phnum;
    elf32_half e_shentsize;
    elf32_half e_shnum;
    elf32_half e_shstrndx;
} __attribute__((packed)) elf32_ehdr;

// ============================================================
// Program header
// ============================================================

typedef struct {
    elf32_word p_type;
    elf32_off  p_offset;
    elf32_addr p_vaddr;
    elf32_addr p_paddr;
    elf32_word p_filesz;
    elf32_word p_memsz;
    elf32_word p_flags;
    elf32_word p_align;
} __attribute__((packed)) elf32_phdr;

// ============================================================
// Enums
// ============================================================

enum elf_ident {
    EI_MAG0       = 0,
    EI_MAG1       = 1,
    EI_MAG2       = 2,
    EI_MAG3       = 3,
    EI_CLASS      = 4,
    EI_DATA       = 5,
    EI_VERSION    = 6,
    EI_OSABI      = 7,
    EI_ABIVERSION = 8,
    EI_PAD        = 9,
};

enum elf_type {
    ET_NONE = 0,
    ET_REL  = 1,
    ET_EXEC = 2,
    ET_DYN  = 3,
};

enum elf_phdr_type {
    PT_NULL = 0,
    PT_LOAD = 1,
};

// ============================================================
// Public API
// ============================================================

int  elf_check_file(elf32_ehdr *hdr);
int  elf_check_supported(elf32_ehdr *hdr);
int  elf_load(const char *name, uint32_t *entry);

#endif