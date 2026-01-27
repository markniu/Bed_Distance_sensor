# Bed leveling sensor BDsensor(Bed Distance sensor)
# https://github.com/markniu/Bed_Distance_sensor
# Copyright (C) 2023 Mark yue <niujl123@sina.com>
# This file may be distributed under the terms of the GNU GPLv3 license.
import sched
import time
import copy
import chelper
import math
from threading import Timer
from mcu import MCU, MCU_trsync
from . import filament_switch_sensor

from . import BDsensor


class WidthSensorBDsensor:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.config = config
        self.reactor = self.printer.get_reactor()
        self.gcode = self.printer.lookup_object("gcode")
        self.width_filament = config.getint("width_filament", 0)
        self.MEASUREMENT_INTERVAL_MM = config.getint("measurement_interval", 10)
        self.use_current_dia_while_delay = config.getboolean(
            "use_current_dia_while_delay", False
        )
        self.nominal_filament_dia = config.getfloat(
            "default_nominal_filament_diameter", 1.75
        )
        self.measurement_delay = config.getfloat("measurement_delay", 1)
        self.measurement_max_difference = config.getfloat("max_difference", 0.2)
        self.max_diameter = self.nominal_filament_dia + self.measurement_max_difference
        self.min_diameter = self.nominal_filament_dia - self.measurement_max_difference
        self.diameter = self.nominal_filament_dia
        self.is_active = config.getboolean("enable", False)
        self.runout_dia_min = config.getfloat("min_diameter", 1.0)
        self.runout_dia_max = config.getfloat("max_diameter", self.max_diameter)
        self.is_active = False  # config.getboolean('enable', False)
        self.extrude_factor_update_timer = self.reactor.register_timer(
            self.extrude_factor_update_event
        )
        self.gcode.register_command("ENABLE_FILAMENT_WIDTH_SENSOR", self.cmd_enable)
        self.gcode.register_command("DISABLE_FILAMENT_WIDTH_SENSOR", self.cmd_disable)
        self.gcode.register_command("CLEAR_FILAMENT_WIDTH_SENSOR", self.cmd_clear)
        self.printer.register_event_handler("klippy:ready", self.handle_ready)
        self.zero_pose = 0
        self.filament_array = []
        self.runout_helper = filament_switch_sensor.RunoutHelper(config)
        self.bdsensor = BDsensor.BDsensorEndstopWrapper(config)
        # Initialization

    def handle_ready(self):
        # Load printer objects
        self.toolhead = self.printer.lookup_object("toolhead")

        # Start extrude factor update timer
        self.reactor.update_timer(self.extrude_factor_update_timer, self.reactor.NOW)

    def cmd_clear(self, gcmd):
        self.zero_pose = 0

    def cmd_enable(self, gcmd):
        response = "Filament width sensor Turned On"
        if self.is_active:
            response = "Filament width sensor is already On"
        else:
            self.is_active = True
            # Start extrude factor update timer
            self.reactor.update_timer(
                self.extrude_factor_update_timer, self.reactor.NOW
            )
        gcmd.respond_info(response)

    def cmd_disable(self, gcmd):
        response = "Filament width sensor Turned Off"
        if not self.is_active:
            response = "Filament width sensor is already Off"
        else:
            self.is_active = False
            # Stop extrude factor update timer
            self.reactor.update_timer(
                self.extrude_factor_update_timer, self.reactor.NEVER
            )
            # Clear filament array
            self.filament_array = []
            # Set extrude multiplier to 100%
            self.gcode.run_script_from_command("M221 S100")
        gcmd.respond_info(response)

    def update_filament_array(self, last_epos):

        self.diameter = self.bdsensor.BD_Sensor_Read(2)
        self.gcode.respond_info("Filament width:%.3f" % (self.diameter))
        # Fill array
        if len(self.filament_array) > 0:
            # Get last reading position in array & calculate next
            # reading position
            next_reading_position = (
                self.filament_array[-1][0] + self.MEASUREMENT_INTERVAL_MM
            )
            if next_reading_position <= (last_epos + self.measurement_delay):
                self.filament_array.append(
                    [last_epos + self.measurement_delay, self.diameter]
                )
                if self.is_log:
                    self.gcode.respond_info("Filament width:%.3f" % (self.diameter))

        else:
            # add first item to array
            self.filament_array.append(
                [self.measurement_delay + last_epos, self.diameter]
            )
            self.firstExtruderUpdatePosition = self.measurement_delay + last_epos

    def extrude_factor_update_event(self, eventtime):
        # Update extrude factor
        pos = self.toolhead.get_position()
        last_epos = pos[3]
        # Update filament array for lastFilamentWidthReading
        self.update_filament_array(last_epos)
        # Check runout
        self.runout_helper.note_filament_present(
            self.runout_dia_min <= self.diameter <= self.runout_dia_max
        )
        # Does filament exists
        if self.diameter > 0.5:
            if len(self.filament_array) > 0:
                # Get first position in filament array
                pending_position = self.filament_array[0][0]
                if pending_position <= last_epos:
                    # Get first item in filament_array queue
                    item = self.filament_array.pop(0)
                    self.filament_width = item[1]
                else:
                    if (self.use_current_dia_while_delay) and (
                        self.firstExtruderUpdatePosition == pending_position
                    ):
                        self.filament_width = self.diameter
                    elif self.firstExtruderUpdatePosition == pending_position:
                        self.filament_width = self.nominal_filament_dia
                if (self.filament_width <= self.max_diameter) and (
                    self.filament_width >= self.min_diameter
                ):
                    percentage = round(
                        self.nominal_filament_dia**2 / self.filament_width**2 * 100
                    )
                    self.gcode.run_script("M221 S" + str(percentage))
                else:
                    self.gcode.run_script("M221 S100")
        else:
            self.gcode.run_script("M221 S100")
            self.filament_array = []

        if self.is_active:
            return eventtime + 1
        else:
            return self.reactor.NEVER


def load_config(config):
    return WidthSensorBDsensor(config)
