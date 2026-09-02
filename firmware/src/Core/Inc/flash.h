
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __FLASH_H__
#define __FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#define u8  unsigned char
#define u16 unsigned short
#define u32 unsigned int
	
#ifndef u64
#define STM32_FLASH_BASE FLASH_BASE + 30*1024
typedef unsigned long long u64;

#endif
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */
void stm32_FLASH_ErasePage(u32 WriteAddr);
u8 STMFLASH_ReadByte(u32 faddr);
void STMFLASH_Write_64(u32 WriteAddr,u8*pBuffer,u16 NumToWrite);
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */



/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __FLAS_H__ */

