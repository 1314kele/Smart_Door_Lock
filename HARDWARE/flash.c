#include "flash.h"
#include "stm32f4xx_flash.h"
#include "string.h"

static flash_data_t flash_data;

static void flash_save_all(void); // 前向声明

void flash_init(void)
{
    flash_data_t *p = (flash_data_t *)FLASH_SAVE_ADDR;
    uint32_t *raw_addr = (uint32_t *)FLASH_SAVE_ADDR;
    uint32_t old_card_uid = 0;
    uint8_t found = 0;
    uint8_t i;
    uint8_t data_valid = 1;
    
    // 先检查是否是我们的新格式
    if(p->magic == FLASH_MAGIC) {
        // 验证数据是否有效
        if(p->card_count > MAX_CARDS || p->password_count > MAX_PASSWORDS) {
            data_valid = 0;
        }
        if(data_valid) {
            // 检查每个密码字符串是否有效（非0xFF填充）
            for(i = 0; i < p->password_count && data_valid; i++) {
                if(p->passwords[i][0] == 0xFF) {
                    data_valid = 0;
                }
            }
        }
        
        if(data_valid) {
            // 有效，直接复制
            memcpy(&flash_data, p, sizeof(flash_data_t));
            // 如果管理密码未设置，设置默认密码
            if(flash_data.admin_password[0] == 0 || flash_data.admin_password[0] == 0xFF) {
                strcpy(flash_data.admin_password, "123456");
                flash_save_all();
            }
        } else {
            // 数据无效，当作旧格式处理
            // 先尝试从旧格式恢复卡号
            memset(&flash_data, 0, sizeof(flash_data_t));
            flash_data.magic = FLASH_MAGIC;
            flash_data.password_count = 0;
            flash_data.card_count = 0;
            strcpy(flash_data.admin_password, "123456"); // 默认管理密码
            
            // 尝试从 raw_addr[1] 读取旧卡号（旧格式可能是 magic + card_uid）
            old_card_uid = raw_addr[1];
            if(old_card_uid != 0xFFFFFFFF && old_card_uid != 0x00000000) {
                flash_data.cards[0] = old_card_uid;
                flash_data.card_count = 1;
            }
            
            // 保存新格式
            flash_save_all();
        }
    } else {
        // 尝试兼容旧的格式
        memset(&flash_data, 0, sizeof(flash_data_t));
        flash_data.magic = FLASH_MAGIC;
        flash_data.password_count = 0;
        flash_data.card_count = 0;
        strcpy(flash_data.admin_password, "123456"); // 默认管理密码
        
        // 如果旧的格式是 magic + carduid，尝试读取
        if(raw_addr[0] != 0xFFFFFFFF) { // 检查是否有数据
            old_card_uid = 0;
            found = 0;
            
            // 尝试多种可能的旧格式
            
            // 格式1: 只存一个卡号在第一个位置
            old_card_uid = raw_addr[0];
            if(old_card_uid != 0xFFFFFFFF && old_card_uid != 0x00000000 && old_card_uid != FLASH_MAGIC) {
                flash_data.cards[0] = old_card_uid;
                flash_data.card_count = 1;
                found = 1;
            }
            
            // 格式2: [magic][card_uid]
            if(!found) {
                old_card_uid = raw_addr[1];
                if(old_card_uid != 0xFFFFFFFF && old_card_uid != 0x00000000) {
                    flash_data.cards[0] = old_card_uid;
                    flash_data.card_count = 1;
                    found = 1;
                }
            }
            
            // 格式3: 尝试查找前16个word中第一个看起来像卡号的值
            if(!found) {
                for(i = 0; i < 16; i++) {
                    old_card_uid = raw_addr[i];
                    if(old_card_uid != 0xFFFFFFFF && old_card_uid != 0x00000000 && old_card_uid != FLASH_MAGIC) {
                        // 看起来像一个可能的卡号
                        flash_data.cards[0] = old_card_uid;
                        flash_data.card_count = 1;
                        found = 1;
                        break;
                    }
                }
            }
            
            // 保存新格式
            flash_save_all();
        }
    }
}

static void flash_save_all(void)
{
    uint32_t *src;
    uint32_t *dest;
    uint32_t words;
    uint32_t i;
    
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    FLASH_EraseSector(FLASH_Sector_11, VoltageRange_3);

    src = (uint32_t *)&flash_data;
    dest = (uint32_t *)FLASH_SAVE_ADDR;
    words = sizeof(flash_data_t) / 4;
    
    for(i = 0; i < words; i++) {
        FLASH_ProgramWord((uint32_t)(dest + i), src[i]);
    }

    FLASH_Lock();
}

uint8_t flash_get_password_count(void)
{
    // 永远不会返回大于 MAX_PASSWORDS 的值
    if(flash_data.password_count > MAX_PASSWORDS) {
        return 0;
    }
    return flash_data.password_count;
}

char* flash_get_password(uint8_t index)
{
    // 检查 index 和 password_count
    if(flash_data.password_count > MAX_PASSWORDS) {
        return NULL;
    }
    if(index < flash_data.password_count) {
        return flash_data.passwords[index];
    }
    return NULL;
}

uint8_t flash_add_password(const char* pwd)
{
    if(flash_data.password_count >= MAX_PASSWORDS) {
        return 0;
    }
    if(strlen(pwd) > PWD_LENGTH) {
        return 0;
    }
    strcpy(flash_data.passwords[flash_data.password_count], pwd);
    flash_data.password_count++;
    flash_save_all();
    return 1;
}

uint8_t flash_delete_password(uint8_t index)
{
    uint8_t i;
    
    if(index >= flash_data.password_count) {
        return 0;
    }
    for(i = index; i < flash_data.password_count - 1; i++) {
        strcpy(flash_data.passwords[i], flash_data.passwords[i + 1]);
    }
    memset(flash_data.passwords[flash_data.password_count - 1], 0, PWD_LENGTH + 1);
    flash_data.password_count--;
    flash_save_all();
    return 1;
}

void flash_clear_all_passwords(void)
{
    memset(flash_data.passwords, 0, sizeof(flash_data.passwords));
    flash_data.password_count = 0;
    flash_save_all();
}

uint8_t flash_get_card_count(void)
{
    // 永远不会返回大于 MAX_CARDS 的值
    if(flash_data.card_count > MAX_CARDS) {
        return 0;
    }
    return flash_data.card_count;
}

uint32_t flash_get_card(uint8_t index)
{
    // 检查 index 和 card_count
    if(flash_data.card_count > MAX_CARDS) {
        return 0;
    }
    if(index < flash_data.card_count) {
        return flash_data.cards[index];
    }
    return 0;
}

uint8_t flash_add_card(uint32_t uid)
{
    if(flash_data.card_count >= MAX_CARDS) {
        return 0;
    }
    flash_data.cards[flash_data.card_count] = uid;
    flash_data.card_count++;
    flash_save_all();
    return 1;
}

uint8_t flash_delete_card(uint8_t index)
{
    uint8_t i;
    
    if(index >= flash_data.card_count) {
        return 0;
    }
    for(i = index; i < flash_data.card_count - 1; i++) {
        flash_data.cards[i] = flash_data.cards[i + 1];
    }
    flash_data.cards[flash_data.card_count - 1] = 0;
    flash_data.card_count--;
    flash_save_all();
    return 1;
}

void flash_clear_all_cards(void)
{
    memset(flash_data.cards, 0, sizeof(flash_data.cards));
    flash_data.card_count = 0;
    flash_save_all();
}

uint32_t saved_card_uid = 0;

uint32_t flash_read_card_uid(void)
{
    if(flash_data.card_count > 0) {
        return flash_data.cards[0];
    }
    return 0;
}

void flash_save_card_uid(uint32_t uid)
{
    if(flash_data.card_count == 0) {
        flash_add_card(uid);
    } else {
        flash_data.cards[0] = uid;
        flash_save_all();
    }
    saved_card_uid = uid;
}

char* flash_get_admin_password(void)
{
    return flash_data.admin_password;
}

void flash_set_admin_password(const char* pwd)
{
    if(strlen(pwd) <= PWD_LENGTH) {
        strcpy(flash_data.admin_password, pwd);
        flash_save_all();
    }
}
