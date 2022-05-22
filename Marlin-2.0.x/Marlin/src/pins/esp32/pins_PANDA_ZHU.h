/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#pragma once

/**
 * Espressif ESP32 (Tensilica Xtensa LX6) pin assignments
 */

#include "env_validate.h"

#define BOARD_INFO_NAME "PANDA_ZHU"
#define DEFAULT_MACHINE_NAME  BOARD_INFO_NAME

#define SERVO0_PIN                       0


#if PANDA_BED
#define  PANDA_BED_SDA  15  
#define  PANDA_BED_SCL  13  
#endif

//
// Limit Switches
//
#define X_MIN_PIN                             4
#define Y_MIN_PIN                             35
#define Z_MIN_PIN                             21

//
// Steppers
//
#define X_STEP_PIN                          100+1 
#define X_DIR_PIN                           100
#define X_ENABLE_PIN                        100+24+4
//#define X_CS_PIN                             0

#define Y_STEP_PIN                          100+3
#define Y_DIR_PIN                           100+2 
#define Y_ENABLE_PIN                        100+24+4
//#define Y_CS_PIN                            13

#define Z_STEP_PIN                           100+5
#define Z_DIR_PIN                            100+4
#define Z_ENABLE_PIN                         100+24+4
//#define Z_CS_PIN                             5  // SS_PIN

#define E0_STEP_PIN                          100+7
#define E0_DIR_PIN                           100+6
#define E0_ENABLE_PIN                        100+24+4
//#define E0_CS_PIN                           21

#define E1_STEP_PIN                          100+8+7
#define E1_DIR_PIN                           100+8+6
#define E1_ENABLE_PIN                        100+24+4

#define E2_STEP_PIN                          100+8+4
#define E2_DIR_PIN                           100+8+5
#define E2_ENABLE_PIN                        100+24+4

#define E3_STEP_PIN                          100+8+2
#define E3_DIR_PIN                           100+8+3
#define E3_ENABLE_PIN                        100+24+4

#define E4_STEP_PIN                          100+16+5
#define E4_DIR_PIN                           100+16+6
#define E4_ENABLE_PIN                        100+24+4


//
// Temperature Sensors
//
#define TEMP_0_PIN                            39  // Analog Input
#define TEMP_BED_PIN                          36  // Analog Input

//
// Heaters / Fans
//
#define HEATER_0_PIN                           100+8
#define HEATER_1_PIN                           123
#define HEATER_BED_PIN                         100+8+1

#define FAN_PIN                                100+16+2 // this is the FAN0 and FAN0_5V on the board.  M106: Set Fan Speed 0~255, E.g "M106 P0 S255"
#define FAN1_PIN                               100+16+3 // this is the FAN1 on the board.              M106: Set Fan Speed 0~255,M107: Fan Off. E.g "M106 P1 S255"
#define E0_AUTO_FAN_PIN_ESP                    100+16+4 // this is the FAN2 on the board.and used to cool the hotend0.it will be opened automatically if the temperature of Hotend0 is higher than 50°C which is configured in the configuration_adv.h.EXTRUDER_AUTO_FAN_TEMPERATURE,E0_AUTO_FAN_PIN_ESP.

//
// MicroSD card
//
#define SD_MOSI_PIN                           23
#define SD_MISO_PIN                           19
#define SD_SCK_PIN                            18
#define SDSS                                   5
#define SD_DETECT_PIN                          2

#define BEEPER_PIN                  100+24+5 
#define BTN_ENC                     12 

#define BTN_EN1                     33 
#define BTN_EN2                     32 

#define LCD_PINS_RS                    27
#define LCD_PINS_ENABLE                26
#define LCD_PINS_D4                    14
 #if HAS_TMC_UART
  //
  // TMC2209 stepper drivers
  //

  //
  // Hardware serial 1
  //
  #define X_HARDWARE_SERIAL              MYSERIAL1
  #define Y_HARDWARE_SERIAL              MYSERIAL1
  #define Z_HARDWARE_SERIAL              MYSERIAL1
  #define E0_HARDWARE_SERIAL             MYSERIAL1

  #define TMC_BAUD_RATE 115200
#endif   

//  Terri  3/2/22  This section added on advice of Yue Mark.  https://www.pandapi3d.com/forum/pandapi-fdm-3d-printer/reprap-discount-screen-showing-garbled-mess/p-1/dl-621ed1f17d1e43001657ee45?origin=notification&postId=621e49f1cc9082001604460a&commentId=621ed1f17d1e43001657ee45
#define BOARD_ST7920_DELAY_1 DELAY_NS(251)
#define BOARD_ST7920_DELAY_2 DELAY_NS(48)
#define BOARD_ST7920_DELAY_3 DELAY_NS(1050)
//  End section