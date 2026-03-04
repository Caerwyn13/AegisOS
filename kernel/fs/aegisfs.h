#ifndef AEGISFS_H
#define AEGISFS_H

#include "types.h"

#define FS_MAGIC      0xAE915F5  // AegisFS magic number
#define FS_MAX_FILES  128
#define FS_MAX_NAME   32
#define FS_MAX_SIZE   (512 * 64)  // 32KB max file size
#define FS_BLOCK_SIZE 512

// superblock lives at LBA 0
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t file_count;
    uint32_t reserved[13]; // pad to 64 bytes
} __attribute__((packed)) fs_superblock_t;

// each inode is 64 bytes, inodes live at LBA 1-16
typedef struct {
    char     name[FS_MAX_NAME];
    uint32_t size;
    uint32_t start_lba;
    uint32_t used;
    uint8_t  reserved[20];
} __attribute__((packed)) fs_inode_t;

void fs_init();
int  fs_create(const char *name);
int  fs_write(const char *name, const uint8_t *data, uint32_t size);
int  fs_read(const char *name, uint8_t *buf, uint32_t *size);
int  fs_delete(const char *name);
void fs_list();
int  fs_exists(const char *name);

#endif