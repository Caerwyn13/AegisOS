#include "aegisfs.h"
#include "ata.h"
#include "vga.h"
#include "string.h"
#include "heap.h"
#include "serial.h"
#include "auth.h"

// layout on disk:
// LBA 0        -- superblock
// LBA 1-16     -- inodes (128 inodes * 64 bytes = 8192 bytes = 16 sectors)
// LBA 17+      -- file data

#define SUPERBLOCK_LBA  0
#define INODE_START_LBA 1
#define DATA_START_LBA  17
#define INODE_METADATA_VERSION 1
#define FS_MODE_DEFAULT 0644
#define FS_MODE_SYSTEM  0600
#define FS_MODE_LEGACY  0644

//#define INODES_PER_SECTOR (FS_BLOCK_SIZE / sizeof(fs_inode_t))
#define INODES_PER_SECTOR 8

static fs_superblock_t sb;
static int fs_ready = 0;

static int valid_name(const char *name) {
    if (!name || !name[0] || strlen(name) >= FS_MAX_NAME)
        return 0;
    return 1;
}

static uint8_t current_uid(void) {
    return auth_is_root() || !auth_is_logged_in()
         ? FS_UID_ROOT : FS_UID_USER;
}

static uint8_t current_gid(void) {
    return auth_is_root() || !auth_is_logged_in()
         ? FS_GID_ROOT : FS_GID_USER;
}

static int inode_permission(const fs_inode_t *inode, uint16_t owner_bit,
                            uint16_t group_bit, uint16_t other_bit) {
    if (auth_is_root())
        return 1;
    if (inode->owner_uid == current_uid())
        return (inode->mode & owner_bit) != 0;
    if (inode->group_gid == current_gid())
        return (inode->mode & group_bit) != 0;
    return (inode->mode & other_bit) != 0;
}

static int inode_can_read(const fs_inode_t *inode) {
    return inode_permission(inode, FS_MODE_OWNER_READ, FS_MODE_GROUP_READ,
                            FS_MODE_OTHER_READ);
}

static int inode_can_write(const fs_inode_t *inode) {
    return inode_permission(inode, FS_MODE_OWNER_WRITE, FS_MODE_GROUP_WRITE,
                            FS_MODE_OTHER_WRITE);
}

static int inode_can_execute(const fs_inode_t *inode) {
    if (auth_is_root())
        return (inode->mode & (FS_MODE_OWNER_EXEC | FS_MODE_GROUP_EXEC |
                               FS_MODE_OTHER_EXEC)) != 0;
    return inode_permission(inode, FS_MODE_OWNER_EXEC, FS_MODE_GROUP_EXEC,
                            FS_MODE_OTHER_EXEC);
}

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
        vga_printf_colour(YELLOW, BLACK,
                          "[  INFO ] Formatting AegisFS filesystem\n");
        memset(&sb, 0, sizeof(sb));
        sb.magic      = FS_MAGIC;
        sb.version    = 1;
        sb.file_count = 0;
        sb_write();

        // read it back immediately to verify
        sb_read();

        // zero out inode area
        uint8_t zeroes[FS_BLOCK_SIZE];
        memset(zeroes, 0, FS_BLOCK_SIZE);
        int i;
        for (i = INODE_START_LBA; i < DATA_START_LBA; i++)
            ata_write(i, 1, zeroes);

        vga_printf_colour(LIGHT_GREEN, BLACK,
                          "[  OK  ] AegisFS formatted successfully\n");
    } else {
        vga_printf_colour(LIGHT_GREEN, BLACK,
                          "[  OK  ] AegisFS mounted (%u files)\n",
                          sb.file_count);
    }

    fs_ready = 1;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        fs_inode_t inode;
        inode_read(i, &inode);
        if (inode.used && inode.metadata_version != INODE_METADATA_VERSION) {
            inode.owner_uid = FS_UID_ROOT;
            inode.group_gid = FS_GID_ROOT;
            inode.mode = FS_MODE_LEGACY;
            inode.metadata_version = INODE_METADATA_VERSION;
            inode_write(i, &inode);
        }
    }
}

static int fs_create_with_owner(const char *name, uint8_t owner_uid,
                                uint16_t mode) {
    if (!fs_ready || !valid_name(name)) return -1;
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
    inode.owner_uid = owner_uid;
    inode.group_gid = owner_uid == FS_UID_ROOT ? FS_GID_ROOT : FS_GID_USER;
    inode.mode = mode;
    inode.metadata_version = INODE_METADATA_VERSION;
    inode_write(idx, &inode);

    sb.file_count++;
    sb_write();
    return 0;
}

int fs_create(const char *name) {
    return fs_create_with_owner(name, current_uid(), FS_MODE_DEFAULT);
}

static int fs_write_internal(const char *name, const uint8_t *data,
                             uint32_t size, int system_access) {
    if (!fs_ready || !valid_name(name) || (!data && size != 0) ||
        size > FS_MAX_SIZE)
        return -1;

    int idx = find_inode(name);
    if (idx < 0) {
        // create if it doesn't exist
        if (fs_create_with_owner(name,
                                 system_access ? FS_UID_ROOT : current_uid(),
                                 system_access ? FS_MODE_SYSTEM :
                                                 FS_MODE_DEFAULT) < 0)
            return -1;
        idx = find_inode(name);
    }
    if (idx < 0) return -1;

    fs_inode_t inode;
    inode_read(idx, &inode);
    if (!system_access && !inode_can_write(&inode))
        return -1;

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

int fs_write(const char *name, const uint8_t *data, uint32_t size) {
    return fs_write_internal(name, data, size, 0);
}

int fs_write_system(const char *name, const uint8_t *data, uint32_t size) {
    return fs_write_internal(name, data, size, 1);
}

static int fs_read_internal(const char *name, uint8_t *buf, uint32_t *size,
                            int system_access) {
    if (!fs_ready || !valid_name(name) || !buf || !size) return -1;

    int idx = find_inode(name);
    if (idx < 0) return -1;

    fs_inode_t inode;
    inode_read(idx, &inode);
    if (!system_access && !inode_can_read(&inode))
        return -1;
    if (inode.size > FS_MAX_SIZE) {
        vga_printf("AegisFS: file size is invalid\n");
        return -1;
    }

    uint32_t sectors = (inode.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    uint32_t i;
    for (i = 0; i < sectors; i++) {
        uint8_t sector[FS_BLOCK_SIZE];
        ata_read(inode.start_lba + i, 1, sector);
        uint32_t offset = i * FS_BLOCK_SIZE;
        uint32_t chunk = (inode.size - offset) < FS_BLOCK_SIZE
            ? (inode.size - offset) : FS_BLOCK_SIZE;
        memcpy(buf + offset, sector, chunk);
    }

    *size = inode.size;
    return 0;
}

int fs_read(const char *name, uint8_t *buf, uint32_t *size) {
    return fs_read_internal(name, buf, size, 0);
}

int fs_read_system(const char *name, uint8_t *buf, uint32_t *size) {
    return fs_read_internal(name, buf, size, 1);
}

int fs_delete(const char *name) {
    if (!fs_ready || !valid_name(name)) return -1;

    int idx = find_inode(name);
    if (idx < 0) return -1;

    fs_inode_t inode;
    inode_read(idx, &inode);
    if (!inode_can_write(&inode))
        return -1;
    memset(&inode, 0, sizeof(inode));
    inode_write(idx, &inode);

    sb.file_count--;
    sb_write();
    return 0;
}

int fs_chmod(const char *name, uint16_t mode) {
    if (!fs_ready || !valid_name(name) || mode > 0777)
        return -1;

    int idx = find_inode(name);
    if (idx < 0)
        return -1;

    fs_inode_t inode;
    inode_read(idx, &inode);
    if (!auth_is_root() && inode.owner_uid != current_uid())
        return -1;

    inode.mode = mode;
    inode_write(idx, &inode);
    return 0;
}

int fs_can_write(const char *name) {
    if (!fs_ready || !valid_name(name))
        return 0;

    int idx = find_inode(name);
    if (idx < 0)
        return 0;

    fs_inode_t inode;
    inode_read(idx, &inode);
    return inode_can_write(&inode);
}

int fs_can_read(const char *name) {
    if (!fs_ready || !valid_name(name))
        return 0;

    int idx = find_inode(name);
    if (idx < 0)
        return 0;

    fs_inode_t inode;
    inode_read(idx, &inode);
    return inode_can_read(&inode);
}

int fs_can_execute(const char *name) {
    if (!fs_ready || !valid_name(name))
        return 0;

    int idx = find_inode(name);
    if (idx < 0)
        return 0;

    fs_inode_t inode;
    inode_read(idx, &inode);
    return inode_can_execute(&inode);
}

void fs_list() {
    if (!fs_ready) return;

    if (sb.file_count == 0) {
        vga_printf("No files found\n");
        return;
    }

    vga_printf_colour(YELLOW, BLACK, "%-32s %-6s %-9s %s\n",
                      "Name", "Owner", "Mode", "Size");
    vga_printf_colour(DARK_GREY, BLACK, "%-32s %-6s %-9s %s\n",
                      "----", "-----", "----", "----");

    int i;
    int found = 0;
    for (i = 0; i < FS_MAX_FILES && found < (int)sb.file_count; i++) {
        fs_inode_t inode;
        inode_read(i, &inode);
        if (inode.used && inode.name[0] != '.') {
            char permissions[10] = {
                inode.mode & 0400 ? 'r' : '-',
                inode.mode & 0200 ? 'w' : '-',
                inode.mode & 0100 ? 'x' : '-',
                inode.mode & FS_MODE_GROUP_READ ? 'r' : '-',
                inode.mode & FS_MODE_GROUP_WRITE ? 'w' : '-',
                inode.mode & FS_MODE_GROUP_EXEC ? 'x' : '-',
                inode.mode & 0004 ? 'r' : '-',
                inode.mode & 0002 ? 'w' : '-',
                inode.mode & 0001 ? 'x' : '-',
                '\0'
            };
            const char *owner = inode.owner_uid == FS_UID_ROOT ? "root" :
                                inode.owner_uid == FS_UID_USER ? "user" :
                                "unknown";
            vga_printf("%-32s %-6s %-9s %u bytes\n", inode.name, owner,
                       permissions, inode.size);
            found++;
        }
    }
}

int fs_exists(const char *name) {
    return valid_name(name) && find_inode(name) >= 0;
}