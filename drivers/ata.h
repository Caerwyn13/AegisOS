#ifndef ATA_H
#define ATA_H

#include "types.h"

#define ATA_SECTOR_SIZE 512

void ata_init();
int  ata_read(uint32_t lba, uint8_t sectors, uint8_t *buf);
int  ata_write(uint32_t lba, uint8_t sectors, uint8_t *buf);

#endif