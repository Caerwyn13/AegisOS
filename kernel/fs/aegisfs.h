#ifndef AEGISFS_H
#define AEGISFS_H

#include "types.h"

#define FS_MAGIC      0xAE915F5  // AegisFS magic number
#define FS_MAX_FILES  128
#define FS_MAX_NAME   32
#define FS_MAX_SIZE   (512 * 64)  // 32KB max file size
#define FS_BLOCK_SIZE 512
#define FS_UID_ROOT   0
#define FS_UID_USER   1
#define FS_GID_ROOT   0
#define FS_GID_USER   1
#define FS_MODE_OWNER_READ  0400
#define FS_MODE_OWNER_WRITE 0200
#define FS_MODE_OWNER_EXEC  0100
#define FS_MODE_GROUP_READ  0040
#define FS_MODE_GROUP_WRITE 0020
#define FS_MODE_GROUP_EXEC  0010
#define FS_MODE_OTHER_READ  0004
#define FS_MODE_OTHER_WRITE 0002
#define FS_MODE_OTHER_EXEC  0001

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
    uint8_t  owner_uid;
    uint8_t  group_gid;
    uint16_t mode;
    uint8_t  metadata_version;
    uint8_t  reserved[15];
} __attribute__((packed)) fs_inode_t;

void fs_init();
int  fs_create(const char *name);
int  fs_write(const char *name, const uint8_t *data, uint32_t size);
int  fs_read(const char *name, uint8_t *buf, uint32_t *size);
int  fs_delete(const char *name);
int  fs_chmod(const char *name, uint16_t mode);
int  fs_can_read(const char *name);
int  fs_can_write(const char *name);
int  fs_can_execute(const char *name);
int  fs_read_system(const char *name, uint8_t *buf, uint32_t *size);
int  fs_write_system(const char *name, const uint8_t *data, uint32_t size);
void fs_list();
int  fs_exists(const char *name);

#endif