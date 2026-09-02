/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
#include "flash.h"
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
#define POINTS 75

int filter_s = 0, filter_n = 0, filter_s_old = 0;
unsigned char num = 0;
unsigned short table_point[POINTS];

/* LED behaviour:
 * 0: off    (disconnected & out of range)
 * 1: on     (disconnected & metal detected)
 * 2: flash  (connected & metal detected)
 * 3: blink  (connected & out of range) */
int led_status = 0;
int connected_flag = 0;            /* set once a host read completes */

/* Firmware version string, streamed to the host when reading address 1016 */
const unsigned char sensor_info[20] = {'V','1','.','2','d',' ','p','a','n','d','a','p','i','3','d','\n','\0'};

/* Busy-wait delay of ~nus, calibrated for the 64 MHz system clock */
void for_delay_us(uint32_t nus)
{
    uint32_t Delay = nus * 64 / 64;
    do
    {
        __NOP();
    }
    while (Delay--);
}

#define delay_ms   HAL_Delay

#define LED_ON  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET)
#define LED_OFF HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET)

/* Software 2-wire slave bus (ported from an 8051 design where CLK was the
 * INT0 pin P3.2 and SDA was the INT1 pin P3.3) */
#define CLK_I2C_R HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11)
#define SDA_I2C_R HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12)

int SDA_int_or_output = 0;         /* 0: PA12 is EXTI input, 1: PA12 is open-drain output */

/* Reconfigure PA12 (SDA) as edge-triggered interrupt input */
void SDA_AS_INT()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin  = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    SDA_int_or_output = 0;
}

/* Reconfigure PA12 (SDA) as open-drain output */
void SDA_AS_output()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin   = GPIO_PIN_12;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull  = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    SDA_int_or_output = 1;
}

/* Drive the SDA line (switches PA12 to output first if needed) */
void SDA_I2C_W(int vl)
{
    if (SDA_int_or_output == 0)
        SDA_AS_output();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, vl);
}

/* Comparator output of the oscillator circuit */
#define Q3_pin HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1)

/* Software emulations of the 8051 EX0/EX1 interrupt-enable bits:
 * EX0 gates the CLK edge interrupt, EX1 gates the SDA edge interrupt */
int EX0 = 0, EX1 = 0;

/* Slave state machine:
 * -2: switch mode (SDA driven as the probe output)
 * -1: idle
 *  0: start condition seen
 *  1: clock high, sampling the R/W bit
 *  2: reading (shifting the 11-bit data word out)
 *  3: writing (shifting the 11-bit address word in)
 *  4: writing data (bits counted only, payload unused)
 *  5: transfer finished */
int I2C_status = -1;
int switch_value = -1;             /* >= 0: switch mode active, holds the trigger threshold */

unsigned short data_addr = 0;      /* last address received from the master (10-bit payload) */
unsigned short data_i = 0;         /* bit counter of the word being shifted */
unsigned short on_data = 0, off_data = 0, on_send_data = 0;
unsigned int i2c_timeout = 0;
unsigned short data_send = 0;

/* Readout selector, also used as a streaming counter:
 * 0:                calibrated distance in 0.1 mm units
 * 1:                raw oscillator value
 * 2 .. POINTS+1:    calibration table entries
 * POINTS+2 .. +21:  version string characters */
unsigned char read_raw = 0;

char tmp_d, need_flash = 0;        /* need_flash: 1 clear table, 2/3 erase+save to flash, 4 reboot */
char flash_flag = 0;
unsigned int i2c_timeout_j = 0;

/* Append an even-parity bit (bit 10) to a 10-bit value */
unsigned short Add_OddEven(unsigned short byte)
{
    unsigned char i;
    unsigned char n;               /* count of set bits */
    unsigned short r;              /* result */

    n = 0;
    for (i = 0; i < 10; i++)
    {
        if (((byte >> i) & 0x01) == 0x01)
        {
            n++;
        }
    }
    if ((n & 0x01) == 0x01)
    {
        r = byte | 0x400;          /* odd count of ones: set the parity bit */
    }
    else
    {
        r = byte | 0x00;           /* even count of ones: leave the parity bit clear */
    }
    return r;
}

#define BYTE_CHECK_OK     0x01
#define BYTE_CHECK_ERR    0x00

/* Verify the even-parity bit (bit 10) of an 11-bit word.
 * Returns BYTE_CHECK_OK when the parity matches, BYTE_CHECK_ERR otherwise. */
unsigned short Check_OddEven(unsigned short byte)
{
    unsigned char i;
    unsigned char n;               /* count of set bits */
    unsigned char r;               /* result */

    n = 0;
    for (i = 0; i < 10; i++)
    {
        if (((byte >> i) & 0x01) == 0x01)
        {
            n++;
        }
    }
    if ((byte >> 10) == (n & 0x01))   /* parity bit must equal the count of ones */
    {
        r = BYTE_CHECK_OK;
    }
    else
    {
        r = BYTE_CHECK_ERR;
    }
    return r;
}

/* Convert a raw oscillator value to a distance using the calibration table.
 * Pass 1 (always): find the nearest calibrated entry (skips 0xffff entries).
 * Pass 2 (Tdata == 1): linear interpolation between neighbouring entries,
 * giving the distance in 0.1 mm units, clamped to 1022. */
unsigned short cover_to_distance(unsigned short raw_data, unsigned char Tdata)
{
    unsigned short d_i, d_j, d_x, i;

    d_j = 0;
    d_x = 0;
    d_i = 0xffff;
    for (i = 0; i < POINTS; i++)
    {
        if (table_point[i] != 0xffff)
        {
            if (raw_data > table_point[i])
                d_j = raw_data - table_point[i];
            else
                d_j = table_point[i] - raw_data;
            if (d_i > d_j)         /* keep the closest match seen so far */
            {
                d_i = d_j;
                d_x = i;
            }
        }
    }
    if (Tdata == 1)
    {
        if (raw_data > table_point[d_x])
        {
            /* interpolate towards the next entry */
            d_i = (raw_data - table_point[d_x]) * 10;
            d_j = ((table_point[d_x + 1] - table_point[d_x]) * 10 / 10);
            if (d_j > 0)
                d_x = d_x * 10 + d_i / d_j;
            else
                d_x = d_x * 10;
        }
        else
        {
            if (d_x >= 1)
            {
                /* interpolate towards the previous entry */
                d_i = (table_point[d_x] - raw_data) * 10;
                d_j = ((table_point[d_x] - table_point[d_x - 1]) * 10 / 10);
                if (d_j > 0)
                    d_x = d_x * 10 - d_i / d_j;
                else
                    d_x = d_x * 10;
            }
        }
    }

    if (d_x > 1023)
        d_x = 1022;
    return d_x;
}

#include <stdio.h>
#define I2C_ADDRESS 0x2F          /* 7-bit address of the excitation chip on hardware I2C1 */
int extt = 0;                     /* last EXTI edge seen, debug aid */

#define COUNTOF(__BUFFER__)   (sizeof(__BUFFER__) / sizeof(*(__BUFFER__)))
#define TXBUFFERSIZE          (COUNTOF(aTxBuffer) - 1)
/* Size of reception buffer */
#define RXBUFFERSIZE          TXBUFFERSIZE

/* CLK edge interrupt handler: runs the software 2-wire slave state machine */
void clk_int_callback(void)
{
    i2c_timeout++;
    switch (I2C_status)
    {
        case -2:                  /* switch mode: a CLK level change exits it */
            if (CLK_I2C_R == 1)
            {
                I2C_status = -1;
                i2c_timeout = 0;
                switch_value = -1;
                EX0 = 0;
                EX1 = 1;
                SDA_AS_INT();
            }
            break;

        case 0:                   /* start seen: wait for CLK low and SDA low */
            if (CLK_I2C_R == 0 && SDA_I2C_R == 0)
            {
                I2C_status = 1;
                i2c_timeout_j = 0;
            }
            else
            {
                i2c_timeout++;
            }
            break;

        case 1:                   /* first clock high: sample the R/W bit */
            if (CLK_I2C_R == 1)
            {
                if (SDA_I2C_R)
                {
                    I2C_status = 2;               /* read */
                    /* select the word to shift out */
                    if (read_raw == 0)
                        data_send = Add_OddEven(on_send_data);
                    else if (read_raw == 1)
                        data_send = Add_OddEven(on_data);
                    else if (read_raw >= 2 && read_raw < (POINTS + 2))
                    {
                        data_send = table_point[read_raw - 2];
                        data_send = Add_OddEven(data_send);
                        read_raw++;
                    }
                    else if (read_raw >= (POINTS + 2) && read_raw < (POINTS + 2 + 20))
                    {
                        data_send = sensor_info[read_raw - 2 - POINTS];
                        data_send = Add_OddEven(data_send);
                        read_raw++;
                    }
                }
                else
                {
                    I2C_status = 3;               /* write */
                    data_addr = 0;
                }
                data_i = 0;
            }
            break;

        case 2:                   /* read: shift data_send out MSB first on CLK low */
            if (CLK_I2C_R == 0)
            {
                if (data_i > 10)
                {
                    data_i = 0;
                    i2c_timeout = 0;
                    I2C_status = 5;
                    connected_flag = 1;
                    break;
                }
                SDA_I2C_W(((data_send >> (10 - data_i)) & 0x01));
                data_i++;
            }
            break;

        case 3:                   /* write: shift the address in MSB first on CLK high */
            if (CLK_I2C_R == 1)
            {
                if (SDA_I2C_R)
                    data_addr = (data_addr | (1 << (10 - data_i)));
                data_i++;
                if (data_i > 10)
                {
                    data_i = 0;
                    I2C_status = 5;
                    EX0 = 0;      /* disable CLK edges */
                    EX1 = 1;      /* enable SDA edges, wait for the stop condition */
                    SDA_AS_INT();
                    if (Check_OddEven(data_addr) == BYTE_CHECK_ERR)
                        break;
                    data_addr = (data_addr & 0x3ff);
                    if (switch_value >= 0 && data_addr < 900)
                    {
                        /* update the probe trigger threshold */
                        switch_value = data_addr;
                        I2C_status = -3;
                        break;
                    }
                    if (data_addr < POINTS)
                    {
                        /* calibration: store the current raw value for this point */
                        table_point[data_addr] = on_data;
                    }
                    else if (data_addr == 1020)          /* read raw data */
                    {
                        read_raw = 1;
                    }
                    else if (data_addr == 1019)          /* start calibration */
                    {
                        need_flash = 1;
                    }
                    else if (data_addr == 1016)          /* read version data */
                    {
                        read_raw = POINTS + 2;
                    }
                    else if (data_addr == 1021)          /* finish calibration: erase and save to flash */
                    {
                        need_flash = 2;
                    }
                    else if (data_addr == 1017)          /* read calibration data */
                    {
                        read_raw = 2;
                    }
                    else if (data_addr == 1018)          /* end reading calibration/version data */
                    {
                        read_raw = 0;
                    }
                    else if (data_addr == 1022)          /* reboot */
                    {
                        need_flash = 4;
                    }
                    else if (data_addr == 1023)          /* enter switch mode: send 1023, send the trigger
                                                           value, then SDA becomes the probe output; a CLK
                                                           level change exits switch mode */
                    {
                        switch_value = 0;
                    }
                }
            }
            break;

        case 4:                   /* write data: count the bits, payload unused */
            if (CLK_I2C_R == 0)
            {
                data_i++;
                if (data_i > 9)
                {
                    data_i = 0;
                    I2C_status = 5;
                }
            }
            break;

        case 5:                   /* transfer finished: arm stop detection */
            EX0 = 0;
            EX1 = 1;
            SDA_AS_INT();
            break;

        default:
            break;
    }
}

/* SDA edge interrupt handler: start/stop condition detection */
void sda_int_callback()
{
    EX0 = 0;

    switch (I2C_status)
    {
        case 0:
        case -1:
            if ((SDA_I2C_R == 0) && CLK_I2C_R)      /* start condition */
            {
                I2C_status = 0;
                data_i = 0;
                EX1 = 0;          /* disable SDA edges */
                EX0 = 1;          /* enable CLK edges */
            }
            break;

        case 2:
        case 4:
        case 5:
            break;

        default:
            break;
    }

    if ((SDA_I2C_R == 1) && CLK_I2C_R)              /* stop condition */
    {
        if (I2C_status == -3)     /* switch-mode threshold just written */
        {
            EX0 = 1;
            EX1 = 0;
            SDA_AS_output();
            i2c_timeout = 0;
            I2C_status = -2;
            return;
        }
        else
            I2C_status = -1;
        EX0 = 0;
        EX1 = 1;
        SDA_AS_INT();
        i2c_timeout = 0;
    }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_11)
    {
        extt = 1;
        if (EX0)
            clk_int_callback();
    }
    else if (GPIO_Pin == GPIO_PIN_12)
    {
        extt = 3;
        if (EX1)
            sda_int_callback();
    }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_11)
    {
        extt = 2;
        if (EX0)
            clk_int_callback();
    }
    else if (GPIO_Pin == GPIO_PIN_12)
    {
        extt = 4;
        if (EX1)
            sda_int_callback();
    }
}

/* Write one command word to the excitation chip on hardware I2C1.
 * The 2-byte frame packs the 6-bit command and the upper bits of DAT. */
char I2C_TransmitData(uint16_t i2c_address, unsigned char command, unsigned short DAT)
{
    uint8_t txbuf[3];

    command = (command << 2) | (DAT >> 8);
    i2c_address = i2c_address << 1;
    txbuf[0] = command;
    txbuf[1] = DAT & 0xff;

    if (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)i2c_address, (uint8_t *)txbuf, 2, 1000) != HAL_OK)
    {
        USART2_printf(" i2c error2\r\n");
        if (HAL_I2C_GetError(&hi2c1) != HAL_I2C_ERROR_AF)
        {
            USART2_printf(" i2c error\r\n");
            return 1;
        }
    }
    return (0);
}

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint8_t aTxBuffer[] = " test  ";   /* test buffer referenced by the TXBUFFERSIZE macro */

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Bus watchdog: reset the slave state machine when a transfer stalls
 * mid-frame, and execute the reboot request (need_flash == 4). */
void time_out_process()
{
    if (I2C_status >= 0)
        i2c_timeout++;
    else
        i2c_timeout = 0;

    if (I2C_status == -1)
        i2c_timeout_j++;
    else
        i2c_timeout_j = 0;

    if ((i2c_timeout > 5000 && I2C_status != -2) || need_flash == 4)   /* 5000 calls is about 1 s */
    {
        need_flash = 0;
        i2c_timeout = 0;
        I2C_status = -1;
        EX0 = 0;
        EX1 = 1;
        SDA_AS_INT();
        NVIC_SystemReset();
    }
}

#define T_HOLD    220            /* legacy tuning constant, currently unused */

void init_T1(void)
{
}

unsigned int T_MAX = 1015;                              /* sweep upper limit: out of range */
unsigned int T_MIN = 10, out_range_count = 0, timer_count = 0;;

#define Del 1                                           /* sweep step size */

/* Background task called from the main loop: drives the switch-mode probe
 * output, the status LED, the bus watchdog and deferred flash operations. */
void command_process(void)
{
    unsigned int i = 0;

    if (I2C_status == -2)
    {
        /* switch mode: drive SDA from the measured distance */
        if (on_data > 750)
            SDA_I2C_W(0);
        if (switch_value != 1)
        {
            if (on_send_data <= switch_value)
                SDA_I2C_W(1);
            else
                SDA_I2C_W(0);
        }
    }
    out_range_count++;
    timer_count++;
    if (out_range_count > 2000)     /* no comparator edge for a while: out of range */
    {
        on_send_data = on_data = T_MAX;
        out_range_count = 0;
        USART2_printf("out of range:%d %d\n", I2C_status, switch_value);
    }

    /* LED status:
     * connected & metal detected  -> fast flash
     * connected & out of range     -> slow flash
     * disconnected & metal         -> solid on
     * disconnected & out of range  -> off */
    if (on_data < 750 && connected_flag && on_data)
    {
        led_status = 2;
        if ((timer_count % 300) == 0)
            GPIOB->ODR ^= (1 << 3);
    }
    else if (on_data > 750 && connected_flag)
    {
        led_status = 3;
        if ((timer_count % 10000) == 0)
            GPIOB->ODR ^= (1 << 3);
    }
    else if (on_data < 900 && (connected_flag == 0) && on_data)
    {
        led_status = 1;
        LED_ON;
    }
    else if (on_data > 900 && (connected_flag == 0))
    {
        led_status = 0;
        LED_OFF;
    }

    time_out_process();

    if (need_flash == 2)            /* calibration finished: erase the storage page */
    {
        need_flash = 3;
        USART2_printf("Erase \n");
        stm32_FLASH_ErasePage(STM32_FLASH_BASE);
        delay_ms(100);
    }
    else if (need_flash == 3)       /* then store the calibration table */
    {
        need_flash = 0;
        USART2_printf("saveing to flash ===\n");
        STMFLASH_Write_64(STM32_FLASH_BASE, (unsigned char *)table_point, POINTS * 2);
        for (i = 0; i < POINTS; i++)
        {
            USART2_printf(" ,%d ", table_point[i]);
            delay_ms(1);
        }
    }
    else if (need_flash == 1)       /* start calibration: clear the table */
    {
        need_flash = 0;
        for (i = 0; i < POINTS; i++)
            table_point[i] = 0xffff;
    }
}

#define C_ON  1                   /* Q3_pin levels */
#define C_OFF 0
#define addr2 0x2f

#define abs_i(a,b) (a>b?(a-b):(b-a))

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
    int i = 0;
    int rdata = 0, of_data = 0;
    int old_on_data, old_of_data;
    int j = 0;
    int ret = 0;
    switch_value = -1;

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
    LED_ON;

    /* reset the excitation/AD chip */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_Delay(100);

    delay_ms(100);

    /* load the calibration table from the last flash page */
    for (i = 0; i < POINTS; i++)
    {
        table_point[i] = STMFLASH_ReadByte(STM32_FLASH_BASE + i * 2 + 1) << 8 | STMFLASH_ReadByte(STM32_FLASH_BASE + i * 2);
        USART2_printf("%d:%u; ", i, table_point[i]);
    }

    USART2_printf((char *)sensor_info);
    rdata = T_MIN;
    on_send_data = on_data = 0;   /*1010*/
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_RESET);
    EX0 = 0;
    EX1 = 1;
    LED_OFF;

    /* initial excitation chip setup */
    I2C_TransmitData(0x2f, 0x07, 0x02);
    I2C_TransmitData(addr2, 0x07, 0x02);

    /* Main loop: triangular sweep of the excitation value. The comparator
     * (Q3_pin) flips when the oscillator starts/stops, and the sweep value
     * at that edge is the raw reading for the current distance. */
    while (1)
    {
        /* background commands processing */
        command_process();

        /* oscillator still running: no target in range, sweep up */
        while (Q3_pin == C_ON)
        {
            command_process();
            ret = I2C_TransmitData(0x2f, 1, rdata);
            if (ret > 0)
                USART2_printf("err0:%d,", ret);
            rdata += j;
            if (rdata <= T_MIN)
            {
                j = Del;
                rdata = T_MIN;
            }
            else if (rdata >= T_MAX)
            {
                j = -Del;
                rdata = T_MAX;
            }
            /* poll for the comparator to trip */
            i = 0;
            while (i < 10)
            {
                for_delay_us(100);
                if (Q3_pin == C_OFF)
                {
                    for_delay_us(1000);
                    break;
                }
                i++;
            }
            if (Q3_pin == C_OFF)
            {
                out_range_count = 0;
                on_data = rdata;   /* captured raw value at the trip point */
                j = Del;
                break;
            }
        }

        /* oscillator stopped: target detected, sweep down until it restarts */
        while (Q3_pin == C_OFF)
        {
            command_process();
            ret = I2C_TransmitData(0x2f, 1, rdata);
            if (ret > 0)
                USART2_printf("err1:%d,", ret);
            rdata += j;
            if (rdata <= T_MIN)
            {
                j = Del;
                rdata = T_MIN;
            }
            else if (rdata >= T_MAX)
            {
                j = -Del;
                rdata = T_MAX;
            }
            i = 0;
            while (i < 10)
            {
                for_delay_us(100);
                if (Q3_pin == C_ON) break;
                i++;
            }
            if (Q3_pin == C_ON)
            {
                out_range_count = 0;
                of_data = rdata;

                /* switch mode 1: filtered press detection on SDA */
                if (I2C_status == -2 && switch_value == 1)
                {
                    filter_s += on_data;
                    filter_n++;
                    if (filter_n >= 1)
                    {
                        filter_n = 0;
                        /* stable within +/-4 counts and in range: press */
                        if ((abs_i(filter_s, filter_s_old) < 4) && on_data < 750)
                        {
                            SDA_I2C_W(1);
                            USART2_printf(" press %d,%d,%d\n", on_data, filter_s, filter_s_old);
                        }
                        else
                        {
                            SDA_I2C_W(0);
                        }
                        if (filter_s != filter_s_old)
                            filter_s_old = filter_s;
                        filter_s = 0;
                    }
                }

                /* new reading: convert and report */
                if (old_on_data != on_data || old_of_data != of_data)
                {
                    old_on_data = on_data;
                    old_of_data = of_data;
                    on_send_data = cover_to_distance(on_data, 1);
                    USART2_printf("%d,%d\n", on_data, I2C_status);
                }
                j = -Del;
                break;
            }
        }
    }


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  return;
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
