#ifndef PET_FLASH_H
#define PET_FLASH_H
#include "pet_logic/edgi/storage_journal.h"

/* Flash数据区域配置
 * 地址: 0x60F00000 - 0x60F1FFFF (128KB)
 * 扇区大小: 4KB
 */
#define PET_FLASH_DATA_ADDRESS  0x60F00000UL
#define PET_FLASH_DATA_SIZE     (128 * 1024)    /* 128KB */
#define PET_FLASH_SECTOR_SIZE   4096            /* 4KB扇区 */

int pet_flash_init(PetJournalIo *io);
int pet_flash_gate_ready(void);
unsigned long pet_flash_parks(void);
#endif
