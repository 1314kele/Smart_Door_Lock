#include "flash.h"
#include "stm32f4xx_flash.h"

static uint32_t saved_uid = 0;

void flash_init(void)
{
    flash_data_t *p = (flash_data_t *)FLASH_SAVE_ADDR;
    if(p->magic == FLASH_MAGIC) {
        saved_uid = p->card_uid;
    }
}

uint32_t flash_read_card_uid(void)
{
    return saved_uid;
}

void flash_save_card_uid(uint32_t uid)
{
    uint32_t data[2];

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    FLASH_EraseSector(FLASH_Sector_11, VoltageRange_3);

    data[0] = FLASH_MAGIC;
    data[1] = uid;

    FLASH_ProgramWord(FLASH_SAVE_ADDR, data[0]);
    FLASH_ProgramWord(FLASH_SAVE_ADDR + 4, data[1]);

    FLASH_Lock();

    saved_uid = uid;
}
