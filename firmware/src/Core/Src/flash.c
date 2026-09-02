#include "flash.h"

/* USER CODE BEGIN 0 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

/* Return the padding needed to round data up to a multiple of 8 */
u8 get_rem_data(u8 data)
{
    u8 re_data = 0;
    switch (data)
    {
        case 0: re_data = 0; break;
        case 1: re_data = 7; break;
        case 2: re_data = 6; break;
        case 3: re_data = 5; break;
        case 4: re_data = 4; break;
        case 5: re_data = 3; break;
        case 6: re_data = 2; break;
        case 7: re_data = 1; break;
    }
    return re_data;
}

/* Erase the flash page containing WriteAddr (must be page aligned) */
void stm32_FLASH_ErasePage(u32 WriteAddr)
{
    if (WriteAddr < STM32_FLASH_BASE || (WriteAddr >= (STM32_FLASH_BASE + FLASH_PAGE_SIZE * FLASH_PAGE_NB)))
        return;                                  /* address out of range */

    HAL_FLASH_Unlock();                          /* unlock flash */

    if (WriteAddr % FLASH_PAGE_SIZE == 0)
    {
        FLASH_EraseInitTypeDef My_Flash = {0};   /* erase configuration */
        My_Flash.TypeErase = FLASH_TYPEERASE_PAGES;                        /* erase by page */
        My_Flash.Page      = (WriteAddr - FLASH_BASE) / FLASH_PAGE_SIZE;   /* page number */
        My_Flash.NbPages   = 1;                                            /* erase a single page */

        uint32_t PageError = 0;                  /* page that failed to erase, if any */
        HAL_FLASHEx_Erase(&My_Flash, &PageError);/* erase the page */
    }
    HAL_FLASH_Lock();                            /* lock flash */
}

/* Read one byte from flash memory */
u8 STMFLASH_ReadByte(u32 faddr)
{
    return *(__IO uint8_t *)(faddr);
}

/* Read NumToRead bytes starting at ReadAddr into pBuffer */
void STMFLASH_Read_Byte(u32 ReadAddr, u8 *pBuffer, u16 NumToRead)
{
    u16 i;
    for (i = 0; i < NumToRead; i++)
    {
        pBuffer[i] = STMFLASH_ReadByte(ReadAddr);   /* read one byte */
        ReadAddr += 1;                              /* advance by one byte */
    }
}

/* Write NumToWrite bytes from pBuffer to flash starting at WriteAddr.
 * The page at WriteAddr is erased first (WriteAddr must be page aligned);
 * the payload is zero-padded to a multiple of 8 bytes and programmed as
 * 64-bit words. */
void STMFLASH_Write_64(u32 WriteAddr, u8 *pBuffer, u16 NumToWrite)
{
    u16 i;
    u64 data_write;
    u16 len = NumToWrite;
    u8  len_flag = 0;

    if (NumToWrite > FLASH_PAGE_SIZE)
        return;
    if ((WriteAddr < FLASH_BASE) || (WriteAddr >= (FLASH_BASE + FLASH_PAGE_SIZE * FLASH_PAGE_NB)))
        return;                                  /* address out of range */

    HAL_FLASH_Unlock();                          /* unlock flash */

    len_flag = len % 8;
    len = len + get_rem_data(len_flag);          /* round length up to a multiple of 8 */

    if (WriteAddr % FLASH_PAGE_SIZE == 0)        /* erase before writing */
    {
        FLASH_EraseInitTypeDef My_Flash = {0};   /* erase configuration */
        My_Flash.TypeErase = FLASH_TYPEERASE_PAGES;                        /* erase by page */
        My_Flash.Page      = (WriteAddr - FLASH_BASE) / FLASH_PAGE_SIZE;   /* page number */
        My_Flash.NbPages   = 1;                                            /* erase a single page */

        uint32_t PageError = 0;                  /* page that failed to erase, if any */
        HAL_FLASHEx_Erase(&My_Flash, &PageError);/* erase the page */
    }

    for (i = 0; i < len / 8; i++)                /* program in 8-byte blocks */
    {
        u32 data_1 = 0;
        u64 data_2 = 0;

        data_1 = (pBuffer[8 * i + 0]) | (pBuffer[8 * i + 1] << 8) | (pBuffer[8 * i + 2] << 16) | (pBuffer[8 * i + 3] << 24);
        data_2 = (pBuffer[8 * i + 4]) | (pBuffer[8 * i + 5] << 8) | (pBuffer[8 * i + 6] << 16) | (pBuffer[8 * i + 7] << 24);
        data_write = data_1 | (data_2 << 32);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, WriteAddr, data_write) == HAL_OK)
            WriteAddr += 8;                      /* advance by 8 bytes */
    }
    HAL_FLASH_Lock();                            /* lock flash */
}
