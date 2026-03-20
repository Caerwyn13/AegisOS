#include "ata.h"
#include "ports.h"
#include "vga.h"
#include "pit.h"

// Primary ATA bus
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERROR        0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LOW      0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HIGH     0x1F5
#define ATA_PRIMARY_DRIVE        0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND      0x1F7

// Status bits
#define ATA_STATUS_BSY  0x80  // busy
#define ATA_STATUS_DRQ  0x08  // data request
#define ATA_STATUS_ERR  0x01  // error

// Commands
#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30

static int detected = 0;

static void ata_wait() {
    // wait for BSY to clear
    while (inb(ATA_PRIMARY_STATUS) & ATA_STATUS_BSY);
}

static int ata_wait_drq() {
    uint8_t status;
    while (1) {
        status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_ERR) return -1;
        if (status & ATA_STATUS_DRQ) return 0;
    }
}

static void ata_select(uint32_t lba, uint8_t sectors) {
    outb(ATA_PRIMARY_DRIVE,        0xE0 | ((lba >> 24) & 0x0F)); // LBA mode, drive 0
    outb(ATA_PRIMARY_ERROR,        0x00);
    outb(ATA_PRIMARY_SECTOR_COUNT, sectors);
    outb(ATA_PRIMARY_LBA_LOW,      (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID,      (uint8_t)((lba >> 8)  & 0xFF));
    outb(ATA_PRIMARY_LBA_HIGH,     (uint8_t)((lba >> 16) & 0xFF));
}

void ata_init() {
    outb(ATA_PRIMARY_DRIVE, 0xA0);

    int i;
    for (i = 0; i < 14; i++) inb(ATA_PRIMARY_STATUS);

    ata_wait();

    outb(ATA_PRIMARY_COMMAND, 0xEC);

    for (i = 0; i < 14; i++) inb(ATA_PRIMARY_STATUS);

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        vga_printf("ATA: no drive detected\n");
        return;
    }

    ata_wait();

    if (inb(ATA_PRIMARY_STATUS) & ATA_STATUS_ERR) {
        vga_printf("ATA: drive error\n");
        return;
    }

    // read and discard identify data to clear the buffer
    uint16_t buf[256];
    for (i = 0; i < 256; i++)
        buf[i] = inw(ATA_PRIMARY_DATA);

    detected = 1;
    vga_printf("ATA: drive detected\n");
}

int ata_read(uint32_t lba, uint8_t sectors, uint8_t *buf) {
    if (!detected) return -1;

    ata_wait();
    ata_select(lba, sectors);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ);

    int s;
    for (s = 0; s < sectors; s++) {
        if (ata_wait_drq() < 0) {
            vga_printf("ATA: read error at LBA %u\n", lba + s);
            return -1;
        }
        int i;
        for (i = 0; i < 256; i++) {
            uint16_t data = inw(ATA_PRIMARY_DATA);
            buf[(s * ATA_SECTOR_SIZE) + (i * 2)]     = (uint8_t)(data & 0xFF);
            buf[(s * ATA_SECTOR_SIZE) + (i * 2) + 1] = (uint8_t)(data >> 8);
        }
    }
    return 0;
}

int ata_write(uint32_t lba, uint8_t sectors, uint8_t *buf) {
    if (!detected) return -1;

    ata_wait();
    ata_select(lba, sectors);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE);

    int s;
    for (s = 0; s < sectors; s++) {
        if (ata_wait_drq() < 0) {
            vga_printf("ATA: write error at LBA %u\n", lba + s);
            return -1;
        }
        int i;
        for (i = 0; i < 256; i++) {
            uint16_t data = buf[(s * ATA_SECTOR_SIZE) + (i * 2)] |
                           ((uint16_t)buf[(s * ATA_SECTOR_SIZE) + (i * 2) + 1] << 8);
            outw(ATA_PRIMARY_DATA, data);
        }
    }

    // flush cache
    outb(ATA_PRIMARY_COMMAND, 0xE7);
    ata_wait();
    return 0;
}