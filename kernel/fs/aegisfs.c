#include "aegisfs.h"
#include "ata.h"
#include "vga.h"
#include "string.h"
#include "heap.h"
#include "serial.h"

// layout on disk:
// LBA 0        -- superblock
// LBA 1-16     -- inodes (128 inodes * 64 bytes = 8192 bytes = 16 sectors)
// LBA 17+      -- file data

#define SUPERBLOCK_LBA  0
#define INODE_START_LBA 1
#define DATA_START_LBA  17

//#define INODES_PER_SECTOR (FS_BLOCK_SIZE / sizeof(fs_inode_t))
#define INODES_PER_SECTOR 8

static fs_superblock_t sb;
static int fs_ready = 0;

// ============================================================
// Internal helpers
// ============================================================

static void sb_read() {
    uint8_t buf[FS_BLOCK_SIZE];
    ata_read(SUPERBLOCK_LBA, 1, buf);
    memcpy(&sb, buf, sizeof(fs_superblock_t));
}

static void sb_write() {
    uint8_t buf[FS_BLOCK_SIZE];
    memset(buf, 0, FS_BLOCK_SIZE);
    memcpy(buf, &sb, sizeof(fs_superblock_t));
    ata_write(SUPERBLOCK_LBA, 1, buf);
}

static void inode_read(int index, fs_inode_t *inode) {
    uint32_t lba    = INODE_START_LBA + (index / INODES_PER_SECTOR);
    uint32_t offset = (index % INODES_PER_SECTOR) * sizeof(fs_inode_t);
    uint8_t  buf[FS_BLOCK_SIZE];
    ata_read(lba, 1, buf);
    memcpy(inode, buf + offset, sizeof(fs_inode_t));
}

static void inode_write(int index, fs_inode_t *inode) {
    uint32_t lba    = INODE_START_LBA + (index / INODES_PER_SECTOR);
    uint32_t offset = (index % INODES_PER_SECTOR) * sizeof(fs_inode_t);
    uint8_t  buf[FS_BLOCK_SIZE];
    ata_read(lba, 1, buf);
    memcpy(buf + offset, inode, sizeof(fs_inode_t));
    ata_write(lba, 1, buf);
}

static int find_inode(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        fs_inode_t inode;
        inode_read(i, &inode);
        if (inode.used && strcmp(inode.name, name) == 0)
            return i;
    }
    return -1;
}

static int find_free_inode() {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        fs_inode_t inode;
        inode_read(i, &inode);
        if (!inode.used) return i;
    }
    return -1;
}

static uint32_t alloc_data_lba(uint32_t sectors_needed) {
    // find highest used LBA and allocate after it
    uint32_t highest = DATA_START_LBA;
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        fs_inode_t inode;
        inode_read(i, &inode);
        if (inode.used) {
            uint32_t end = inode.start_lba + ((inode.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE);
            if (end > highest) highest = end;
        }
    }
    (void)sectors_needed;
    return highest;
}

// ============================================================
// Public API
// ============================================================

void fs_init() {
    sb_read();

    if (sb.magic != FS_MAGIC) {
        vga_printf("formatting...\n");
        memset(&sb, 0, sizeof(sb));
        sb.magic      = FS_MAGIC;
        sb.version    = 1;
        sb.file_count = 0;
        sb_write();

        // read it back immediately to verify
        sb_read();
        vga_printf("magic after format: 0x%x\n", sb.magic);

        // zero out inode area
        uint8_t zeroes[FS_BLOCK_SIZE];
        memset(zeroes, 0, FS_BLOCK_SIZE);
        int i;
        for (i = INODE_START_LBA; i < DATA_START_LBA; i++)
            ata_write(i, 1, zeroes);

        vga_printf("AegisFS: formatted successfully\n");
    } else {
        vga_printf("AegisFS: mounted, %u files\n", sb.file_count);
    }

    fs_ready = 1;
}

int fs_create(const char *name) {
    if (!fs_ready) return -1;
    if (find_inode(name) >= 0) return -1; // already exists

    int idx = find_free_inode();
    if (idx < 0) {
        vga_printf("AegisFS: no free inodes\n");
        return -1;
    }

    fs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    strcpy(inode.name, name);
    inode.size      = 0;
    inode.start_lba = alloc_data_lba(0);
    inode.used      = 1;
    inode_write(idx, &inode);

    sb.file_count++;
    sb_write();
    return 0;
}

int fs_write(const char *name, const uint8_t *data, uint32_t size) {
    if (!fs_ready) return -1;

    int idx = find_inode(name);
    if (idx < 0) {
        // create if it doesn't exist
        if (fs_create(name) < 0) return -1;
        idx = find_inode(name);
    }

    if (size > FS_MAX_SIZE) {
        vga_printf("AegisFS: file too large\n");
        return -1;
    }

    fs_inode_t inode;
    inode_read(idx, &inode);

    uint32_t sectors = (size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    uint32_t i;
    for (i = 0; i < sectors; i++) {
        uint8_t buf[FS_BLOCK_SIZE];
        memset(buf, 0, FS_BLOCK_SIZE);
        uint32_t offset = i * FS_BLOCK_SIZE;
        uint32_t chunk  = (size - offset) < FS_BLOCK_SIZE ? (size - offset) : FS_BLOCK_SIZE;
        memcpy(buf, data + offset, chunk);
        ata_write(inode.start_lba + i, 1, buf);
    }

    inode.size = size;
    inode_write(idx, &inode);
    return 0;
}

int fs_read(const char *name, uint8_t *buf, uint32_t *size) {
    if (!fs_ready) return -1;

    int idx = find_inode(name);
    if (idx < 0) return -1;

    fs_inode_t inode;
    inode_read(idx, &inode);

    uint32_t sectors = (inode.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    uint32_t i;
    for (i = 0; i < sectors; i++) {
        uint8_t sector[FS_BLOCK_SIZE];
        ata_read(inode.start_lba + i, 1, sector);
        memcpy(buf + (i * FS_BLOCK_SIZE), sector, FS_BLOCK_SIZE);
    }

    *size = inode.size;
    return 0;
}

int fs_delete(const char *name) {
    if (!fs_ready) return -1;

    int idx = find_inode(name);
    if (idx < 0) return -1;

    fs_inode_t inode;
    inode_read(idx, &inode);
    memset(&inode, 0, sizeof(inode));
    inode_write(idx, &inode);

    sb.file_count--;
    sb_write();
    return 0;
}

void fs_list() {
    if (!fs_ready) return;

    if (sb.file_count == 0) {
        vga_printf("No files found\n");
        return;
    }

    vga_printf_colour(YELLOW,    BLACK, "%-32s %s\n", "Name", "Size");
    vga_printf_colour(DARK_GREY, BLACK, "%-32s %s\n", "----", "----");

    int i;
    int found = 0;
    for (i = 0; i < FS_MAX_FILES && found < (int)sb.file_count; i++) {
        fs_inode_t inode;
        inode_read(i, &inode);
        if (inode.used) {
            vga_printf("%-32s %u bytes\n", inode.name, inode.size);
            found++;
        }
    }
}

int fs_exists(const char *name) {
    return find_inode(name) >= 0;
}