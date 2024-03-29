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
 * CREALITY v4.2.7 (STM32F103RE / STM32F103RC) board pin assignments
 */

#define BOARD_INFO_NAME      "Creality v4.2.7"
#define DEFAULT_MACHINE_NAME "Creality3D"

//
// Steppers
//
#define X_STEP_PIN                          PB9
#define X_DIR_PIN                           PC2

#define Y_STEP_PIN                          PB7
#define Y_DIR_PIN                           PB8

#define Z_STEP_PIN                          PB5
#define Z_DIR_PIN                           PB6

#define E0_STEP_PIN                         PB3
#define E0_DIR_PIN                          PB4

#include "pins_CREALITY_V4.h"
#define  I2C_BD_SDA_PIN    PB1   // Please change to the actual number which the SDA wire is connected to your mainboard
#define  I2C_BD_SCL_PIN    PB0   // Please change to the actual number which the SCL wire is connected to your mainboard
#define  I2C_BD_DELAY  20 