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
from . import manual_probe
from . import probe
BD_TIMER = 0.600
TRSYNC_TIMEOUT = 0.025
TRSYNC_SINGLE_MCU_TIMEOUT = 0.250

CMD_READ_DATA = 1015
CMD_READ_VERSION = 1016
CMD_START_READ_CALIBRATE_DATA = 1017
CMD_DISTANCE_MODE = 1018
CMD_START_CALIBRATE = 1019
CMD_DISTANCE_RAWDATA_TYPE = 1020
CMD_END_CALIBRATE = 1021
CMD_REBOOT_SENSOR = 1022
CMD_SWITCH_MODE = 1023
DATA_ERROR = 1024
CMD_READ_ENDSTOP = 1033


# Calculate a move's accel_t, cruise_t, and cruise_v
def calc_move_time(dist, speed, accel):
    axis_r = 1.
    if dist < 0.:
        axis_r = -1.
        dist = -dist
    if not accel or not dist:
        return axis_r, 0., dist / speed, speed
    max_cruise_v2 = dist * accel
    if max_cruise_v2 < speed ** 2:
        speed = math.sqrt(max_cruise_v2)
    accel_t = speed / accel
    accel_decel_d = accel_t * speed
    cruise_t = (dist - accel_decel_d) / speed
    return axis_r, accel_t, cruise_t, speed

# Helper to obtain a single probe measurement
def run_single_probe(probe, gcmd):
    probe_session = probe.start_probe_session(gcmd)
    probe_session.run_probe(gcmd)
    pos = probe_session.pull_probed_results()[0]
    probe_session.end_probe_session()
    return pos

# I2C BD_SENSOR
# devices connected to an MCU via an virtual i2c bus(2 any gpio)

HINT_TIMEOUT = """
If the probe did not move far enough to trigger, then
consider reducing the Z axis minimum position so the probe
can travel further (the Z minimum position can be negative).
"""


class BDPrinterProbe:
    def __init__(self, config, mcu_probe):
        self.printer = config.get_printer()
        self.name = config.get_name()
        self.config = config
        self.mcu_probe = mcu_probe
        self.speed = config.getfloat('speed', 5.0, above=0.)
        self.lift_speed = config.getfloat('lift_speed', self.speed, above=0.)
        self.x_offset = config.getfloat('x_offset', 0.)
        self.y_offset = config.getfloat('y_offset', 0.)
        self.z_offset = config.getfloat('z_offset')
        self.probe_calibrate_z = 0.
        self.multi_probe_pending = False
        self.rapid_scan = False
        self.last_state = False
        self.last_z_result = 0.
        self.homing_speed_tmp = 0
        self.gcode_move = self.printer.load_object(config, "gcode_move")
        # Infer Z position to move to during a probe
        if config.has_section('stepper_z'):
            zconfig = config.getsection('stepper_z')
            self.z_position = zconfig.getfloat('position_min', 0.,
                                               note_valid=False)
        else:
            pconfig = config.getsection('printer')
            self.z_position = pconfig.getfloat('minimum_z_position', 0.,
                                               note_valid=False)
        # Multi-sample support (for improved accuracy)
        self.sample_count = config.getint('samples', 1, minval=1)
        self.sample_retract_dist = config.getfloat('sample_retract_dist', 5.,
                                                   above=0.)
        self.samples_result = config.getchoice(
            'samples_result',
            {'median': 'median', 'average': 'average'},
            'average'
        )
        self.samples_tolerance = config.getfloat('samples_tolerance', 0.100,
                                                 minval=0.)
        self.samples_retries = config.getint('samples_tolerance_retries', 0,
                                             minval=0)
        # Register z_virtual_endstop pin
        self.printer.lookup_object('pins').register_chip('probe', self)

        # Register homing event handlers
        self.printer.register_event_handler("homing:homing_move_begin",
                                            self._handle_homing_move_begin)
        self.printer.register_event_handler("homing:homing_move_end",
                                            self._handle_homing_move_end)
        self.printer.register_event_handler("homing:home_rails_begin",
                                            self._handle_home_rails_begin)
        self.printer.register_event_handler("homing:home_rails_end",
                                            self._handle_home_rails_end)
        self.printer.register_event_handler("gcode:command_error",
                                            self._handle_command_error)
        # Register PROBE/QUERY_PROBE commands
        self.gcode = self.printer.lookup_object('gcode')
        self.gcode.register_command('PROBE', self.cmd_PROBE,
                                    desc=self.cmd_PROBE_help)
        self.gcode.register_command('QUERY_PROBE', self.cmd_QUERY_PROBE,
                                    desc=self.cmd_QUERY_PROBE_help)
        self.gcode.register_command('PROBE_CALIBRATE',
                                    self.cmd_PROBE_CALIBRATE,
                                    desc=self.cmd_PROBE_CALIBRATE_help)
        self.gcode.register_command('PROBE_ACCURACY', self.cmd_PROBE_ACCURACY,
                                    desc=self.cmd_PROBE_ACCURACY_help)
        self.gcode.register_command(
            'Z_OFFSET_APPLY_PROBE',
            self.cmd_Z_OFFSET_APPLY_PROBE,
            desc=self.cmd_Z_OFFSET_APPLY_PROBE_help
        )
        gcode = self.printer.lookup_object('gcode')
        self.dummy_gcode_cmd = gcode.create_gcode_command("", "", {})
        self.reactor = self.printer.get_reactor()
        self.bd_sample_timer = self.reactor.register_timer(
            self.scan_sample_event)
        
    def _probe_state_error(self):
        raise self.printer.command_error(
                "Internal probe error - start/end probe session mismatch")

    def start_probe_session(self, gcmd):   
        self._probe_times=[]
        if "BED_MESH_CALIBRATE" in gcmd.get_command():
            try:
                if self.mcu_probe.no_stop_probe is not None:
                    self.rapid_scan = True
                    self.reactor.update_timer(self.bd_sample_timer, self.reactor.NOW) 
            except AttributeError as e:
                gcmd.respond_info("%s" % str(e))
                raise gcmd.error("%s" % str(e))
        if self.multi_probe_pending:
            self._probe_state_error()
        self.mcu_probe.multi_probe_begin()
        self.multi_probe_pending = True
        self.mcu_probe.results = []
        return self
        
    def pull_probed_results(self):
        toolhead = self.printer.lookup_object("toolhead")
        toolhead.get_last_move_time()
        if self.rapid_scan == True:    
            self.bedmesh = self.printer.lookup_object('bed_mesh', None)
            helperc = self.bedmesh.bmc.probe_mgr.probe_helper
            while len(self.mcu_probe.results) < len(helperc.probe_points):
                toolhead.dwell(0.1)
            self.reactor.update_timer(self.bd_sample_timer, self.reactor.NEVER)
            self.rapid_scan = False
        res = self.mcu_probe.results
        self.mcu_probe.results = []
        return res

    def end_probe_session(self):
        if not self.multi_probe_pending:
            self._probe_state_error()
        self.mcu_probe.results = []    
        self.multi_probe_pending = False
        self.mcu_probe.multi_probe_end()
        
    def get_probe_params(self, gcmd=None):
        if gcmd is None:
            gcmd = self.dummy_gcode_cmd
        probe_speed = gcmd.get_float("PROBE_SPEED", self.speed, above=0.)
        lift_speed = gcmd.get_float("LIFT_SPEED", self.lift_speed, above=0.)
        samples = gcmd.get_int("SAMPLES", self.sample_count, minval=1)
        sample_retract_dist = gcmd.get_float("SAMPLE_RETRACT_DIST",
                                             self.sample_retract_dist, above=0.)
        samples_tolerance = gcmd.get_float("SAMPLES_TOLERANCE",
                                           self.samples_tolerance, minval=0.)
        samples_retries = gcmd.get_int("SAMPLES_TOLERANCE_RETRIES",
                                       self.samples_retries, minval=0)
        samples_result = gcmd.get("SAMPLES_RESULT", self.samples_result)
        return {'probe_speed': probe_speed,
                'lift_speed': lift_speed,
                'samples': samples,
                'sample_retract_dist': sample_retract_dist,
                'samples_tolerance': samples_tolerance,
                'samples_tolerance_retries': samples_retries,
                'samples_result': samples_result}


    def _handle_homing_move_begin(self, hmove):
        if self.mcu_probe in hmove.get_mcu_endstops():
            self.mcu_probe.probe_prepare(hmove)

    def _handle_homing_move_end(self, hmove):
        if self.mcu_probe in hmove.get_mcu_endstops():
            self.mcu_probe.probe_finish(hmove)

    def _handle_home_rails_begin(self, homing_state, rails):
        endstops = [es for rail in rails for es, name in rail.get_endstops()]
        self.mcu_probe.homing = 1
        if self.mcu_probe in endstops:
            self.mcu_probe.multi_probe_begin()
            self.multi_probe_pending = True
            for i, rail in enumerate(rails):
                if (self.mcu_probe.collision_homing == 1 
                          and rail.homing_retract_dist == 0):
                    self.homing_speed_tmp = rail.homing_speed
                    rail.homing_speed = rail.second_homing_speed
                    self.gcode.respond_info("Homing_speed at %.3f" % (rail.homing_speed))

    def _handle_home_rails_end(self, homing_state, rails):
        endstops = [es for rail in rails for es, name in rail.get_endstops()]
        if self.mcu_probe in endstops:
            for i, rail in enumerate(rails):
                if (self.homing_speed_tmp > rail.homing_speed
                         and rail.homing_retract_dist == 0):
                    rail.homing_speed = self.homing_speed_tmp
            self.multi_probe_end()
            

    def _handle_command_error(self):
        try:
            self.multi_probe_end()
        except:
            raise Exception("Multi-probe end")

    def multi_probe_begin(self):
        self.mcu_probe.multi_probe_begin()
        self.multi_probe_pending = True

    def multi_probe_end(self):
        if self.multi_probe_pending:
            self.multi_probe_pending = False
            self.mcu_probe.multi_probe_end()

    def setup_pin(self, pin_type, pin_params):
        if pin_type != 'endstop' or pin_params['pin'] != 'z_virtual_endstop':
            raise pins.error("Probe virtual endstop only"
                             "useful as endstop pin")
        if pin_params['invert'] or pin_params['pullup']:
            raise pins.error("Can not pullup/invert probe virtual endstop")
        return self.mcu_probe

    def get_lift_speed(self, gcmd=None):
        if gcmd is not None:
            return gcmd.get_float("LIFT_SPEED", self.lift_speed, above=0.)
        return self.lift_speed

    def get_offsets(self):
        return self.x_offset, self.y_offset, self.z_offset

    def _probe_enstop(self, speed):
        toolhead = self.printer.lookup_object('toolhead')
        curtime = self.printer.get_reactor().monotonic()
        if 'z' not in toolhead.get_status(curtime)['homed_axes']:
            raise self.printer.command_error("Must home before probe")
        phoming = self.printer.lookup_object('homing')
        pos = toolhead.get_position()
        pos[2] = self.z_position
        try:
            epos = phoming.probing_move(self.mcu_probe, pos, speed)
        except self.printer.command_error as e:
            reason = str(e)
            if "Timeout during endstop homing" in reason:
                reason += HINT_TIMEOUT
            raise self.printer.command_error(reason)
        # Allow axis_twist_compensation to update results
        self.printer.send_event("probe:update_results", epos)
        # add z compensation to probe position
        gcode = self.printer.lookup_object('gcode')
        gcode.respond_info("probe at %.3f,%.3f is z=%.6f"
                           % (epos[0], epos[1], epos[2]))
        return epos[:3]

    def _probe(self, speed):
        self.mcu_probe.homing = 0
        if self.mcu_probe.endstop_pin_num != self.mcu_probe.sda_pin_num:
            return self._probe_enstop(speed)
        toolhead = self.printer.lookup_object('toolhead')
        curtime = self.printer.get_reactor().monotonic()
        if 'z' not in toolhead.get_status(curtime)['homed_axes']:
            raise self.printer.command_error("Must home before probe")
        phoming = self.printer.lookup_object('homing')
        pos = toolhead.get_position()
        self.mcu_probe.reactor.update_timer(self.mcu_probe.bd_update_timer, 
                                   self.mcu_probe.reactor.NEVER)
        pos[2] = self.z_position
        try:
            epos = phoming.probing_move(self.mcu_probe, pos, speed)
        except self.printer.command_error as e:
            reason = str(e)
            if "Timeout during endstop homing" in reason:
                reason += HINT_TIMEOUT
            raise self.printer.command_error(reason)
        # self.mcu_probe.adjust_probe()
        toolhead.wait_moves()
        time.sleep(0.1)
        b_value = self.mcu_probe.BD_Sensor_Read(2)
        b_value = b_value+self.mcu_probe.BD_Sensor_Read(2)
        b_value = b_value+self.mcu_probe.BD_Sensor_Read(2)
        b_value = b_value/3
        pos_new = toolhead.get_position()
        epos[2] = pos_new[2] - b_value + self.mcu_probe.endstop_bdsensor_offset
        # Allow axis_twist_compensation to update results
        self.printer.send_event("probe:update_results", epos)
        self.gcode.respond_info(
            "probe at %.3f,%.3f is z=%.6f (pos:%.6f - bd:%.3f)"
            % (epos[0], epos[1], epos[2], pos_new[2], b_value)
        )
        #self.mcu_probe.homeing = 0
        return epos[:3]

    def _move(self, coord, speed):
        self.printer.lookup_object('toolhead').manual_move(coord, speed)

    def _calc_mean(self, positions):
        count = float(len(positions))
        return [sum([pos[i] for pos in positions]) / count
                for i in range(3)]

    def _calc_median(self, positions):
        z_sorted = sorted(positions, key=(lambda p: p[2]))
        middle = len(positions) // 2
        if (len(positions) & 1) == 1:
            # odd number of samples
            return z_sorted[middle]
        # even number of samples
        return self._calc_mean(z_sorted[middle - 1:middle + 1])

      
    def _lookup_toolhead_pos(self, pos_time):
        toolhead = self.printer.lookup_object('toolhead')
        kin = toolhead.get_kinematics()
        kin_spos = {s.get_name(): s.mcu_to_commanded_position(
                                      s.get_past_mcu_position(pos_time))
                    for s in kin.get_steppers()}
        return kin.calc_position(kin_spos)
    
    def scan_sample_event(self, eventtime):
        toolhead = self.printer.lookup_object("toolhead")
        while self._probe_times:
            pos_time = self._probe_times[0]
            while 1:
                systime = toolhead.reactor.monotonic()
                est_print_time = toolhead.mcu.estimated_print_time(systime)
                if est_print_time>=pos_time:
                    pos = self._lookup_toolhead_pos(pos_time)
                    intd = self.mcu_probe.BD_Sensor_Read(0)                    
                    pos[2] = pos[2] - intd + self.mcu_probe.endstop_bdsensor_offset
                    self.mcu_probe.results.append(pos)
                    # Allow axis_twist_compensation to update results
                    self.printer.send_event("probe:update_results", pos)
                    # limit the message output to the console else it may take a lot of time
                    if len(self.mcu_probe.results) < 500:
                        self.gcode.respond_info("probe at %.3f,%.3f is z=%.6f"
                                            % (pos[0], pos[1], pos[2]))
                    break
                toolhead.reactor.pause(systime + 0.002)
            self._probe_times.pop(0)
            
        return eventtime + 0.005
            

    def _scan_lookahead_cb(self, printtime):       
        self._probe_times.append(printtime)

    def run_probe(self, gcmd):
        toolhead = self.printer.lookup_object("toolhead")
        if self.rapid_scan == True:
            if len(self._probe_times) == 0:
                toolhead.wait_moves() 
            toolhead.register_lookahead_callback(self._scan_lookahead_cb)
            return
        speed = gcmd.get_float("PROBE_SPEED", self.speed, above=0.)
        lift_speed = self.get_lift_speed(gcmd)

        sample_count = gcmd.get_int("SAMPLES", self.sample_count, minval=1)
        sample_retract_dist = gcmd.get_float("SAMPLE_RETRACT_DIST",
                                             self.sample_retract_dist,
                                             above=0.)
        samples_tolerance = gcmd.get_float("SAMPLES_TOLERANCE",
                                           self.samples_tolerance, minval=0.)
        samples_retries = gcmd.get_int("SAMPLES_TOLERANCE_RETRIES",
                                       self.samples_retries, minval=0)
        samples_result = gcmd.get("SAMPLES_RESULT", self.samples_result)

        must_notify_multi_probe = not self.multi_probe_pending
        if must_notify_multi_probe:
            self.start_probe_session(gcmd)
        probexy = self.printer.lookup_object('toolhead').get_position()[:2]
        retries = 0
        positions = []
        toolhead = self.printer.lookup_object('toolhead')
        # gcmd.respond_info("speed:%.3f"%speed)
        while len(positions) < sample_count:
            # Probe position
            try:
                if ((self.mcu_probe is not None) and
                        (("BED_MESH_CALIBRATE" in gcmd.get_command()) or
                         (("QUAD_GANTRY_LEVEL" in gcmd.get_command() or
                           "Z_TILT_ADJUST" in gcmd.get_command())
                         and self.mcu_probe.QGL_Tilt_Probe == 0))):
                    # pos = self._probe(speed)
                    toolhead.wait_moves()
                    time.sleep(0.1)
                    pos = toolhead.get_position()
                    intd = self.mcu_probe.BD_Sensor_Read(0)
                    pos[2] = pos[2] - intd + self.mcu_probe.endstop_bdsensor_offset
                    self.gcode.respond_info("probe at %.3f,%.3f is z=%.6f"
                                            % (pos[0], pos[1], pos[2]))
                    # return pos[:3]
                    positions.append(pos[:3])
                    # Check samples tolerance
                    z_positions = [p[2] for p in positions]
                    if max(z_positions) - min(z_positions) > samples_tolerance:
                        if retries >= samples_retries:
                            raise gcmd.error("Probe samples "
                                             "exceed samples_tolerance")
                        gcmd.respond_info("Probe samples exceed tolerance."
                                          "Retrying...")
                        retries += 1
                        positions = []
                    continue
            except Exception as e:
                gcmd.respond_info("%s" % str(e))
                raise gcmd.error("%s" % str(e))
                # pass
            pos = self._probe(speed)
            positions.append(pos)
            # Check samples tolerance
            z_positions = [p[2] for p in positions]
            if max(z_positions) - min(z_positions) > samples_tolerance:
                if retries >= samples_retries:
                    raise gcmd.error("Probe samples exceed samples_tolerance")
                gcmd.respond_info("Probe samples exceed tolerance. Retrying...")
                retries += 1
                positions = []
            # Retract
            if len(positions) < sample_count:
                self._move(probexy + [pos[2] + sample_retract_dist], lift_speed)
        if must_notify_multi_probe:
            self.multi_probe_end()
        # Calculate and return result
        if samples_result == 'median':
            return self._calc_median(positions)
        epos = self._calc_mean(positions)
        #epos = calc_probe_z_average(positions, params['samples_result'])
        self.mcu_probe.results.append(epos)

    cmd_PROBE_help = "Probe Z-height at current XY position"

    def cmd_PROBE(self, gcmd):
        pos = run_single_probe(self, gcmd)
        gcmd.respond_info("Result is z=%.6f" % (pos[2],))
        self.last_z_result = pos[2]

    cmd_QUERY_PROBE_help = "Return the status of the z-probe"

    def cmd_QUERY_PROBE(self, gcmd):
        toolhead = self.printer.lookup_object('toolhead')
        print_time = toolhead.get_last_move_time()
        res = self.mcu_probe.query_endstop(print_time)
        self.last_state = res
        gcmd.respond_info("probe: %s" % (["open", "TRIGGERED"][not not res],))

    def get_status(self, eventtime):
        return {'name': self.name,
                'last_query': self.last_state,
                'last_z_result': self.last_z_result}

    cmd_PROBE_ACCURACY_help = "Probe Z-height accuracy at current XY position"

    def cmd_PROBE_ACCURACY(self, gcmd):
        params = self.get_probe_params(gcmd)
        speed = gcmd.get_float("PROBE_SPEED", self.speed, above=0.)
        lift_speed = self.get_lift_speed(gcmd)
        sample_count = gcmd.get_int("SAMPLES", 10, minval=1)
        sample_retract_dist = gcmd.get_float("SAMPLE_RETRACT_DIST",
                                             self.sample_retract_dist, above=0.)
        toolhead = self.printer.lookup_object('toolhead')
        pos = toolhead.get_position()
        pos[2] = 1.0
        gcmd.respond_info("PROBE_ACCURACY at X:%.3f Y:%.3f Z:%.3f"
                          " (samples=%d retract=%.3f"
                          " speed=%.1f lift_speed=%.1f)\n"
                          % (pos[0], pos[1], pos[2],
                             sample_count, sample_retract_dist,
                             speed, lift_speed))
        # Probe bed sample_count times
        
        # toolhead.manual_move([None, None, pos[2]], speed)
        # toolhead.wait_moves()
        fo_params = dict(gcmd.get_command_parameters())
        fo_params['SAMPLES'] = '1'
        fo_gcmd = self.gcode.create_gcode_command("", "", fo_params)
        probe_session = self.start_probe_session(fo_gcmd)
        positions = []
        probe_num = 0
        while probe_num < sample_count:
            # Probe position
            probe_session.run_probe(fo_gcmd)
            probe_num += 1
            # Retract
            pos = toolhead.get_position()
            liftpos = [None, None, pos[2] + params['sample_retract_dist']]
            self._move(liftpos, params['lift_speed'])
        positions = probe_session.pull_probed_results()
        probe_session.end_probe_session()

        # Calculate maximum, minimum and average values
        max_value = max([p[2] for p in positions])
        min_value = min([p[2] for p in positions])
        range_value = max_value - min_value
        avg_value = self._calc_mean(positions)[2]
        median = self._calc_median(positions)[2]
        # calculate the standard deviation
        deviation_sum = 0
        for i in range(len(positions)):
            deviation_sum += pow(positions[i][2] - avg_value, 2.)
        sigma = (deviation_sum / len(positions)) ** 0.5
        # Show information
        gcmd.respond_info(
            "probe accuracy results: maximum %.6f, minimum %.6f, range %.6f, "
            "average %.6f, median %.6f, standard deviation %.6f"
            % (max_value, min_value, range_value, avg_value, median, sigma)
        )

    def probe_calibrate_finalize(self, kin_pos):
        if kin_pos is None:
            return

        z_offset = self.probe_calibrate_z - kin_pos[2]
        self.gcode.respond_info(
            "%s: z_offset: %.3f\n"
            "The SAVE_CONFIG command will update the printer config file\n"
            "with the above and restart the printer." % (self.name, z_offset)
        )
        configfile = self.printer.lookup_object('configfile')
        configfile.set(self.name, 'z_offset', "%.3f" % z_offset)

    cmd_PROBE_CALIBRATE_help = "Calibrate the probe's z_offset"

    def cmd_PROBE_CALIBRATE(self, gcmd):
        manual_probe.verify_no_manual_probe(self.printer)
        # Perform initial probe
        lift_speed = self.get_lift_speed(gcmd)
        params = self.get_probe_params(gcmd)
        curpos = run_single_probe(self, gcmd)
        # Move away from the bed
        self.probe_calibrate_z = curpos[2]
        curpos[2] += 5.
        self._move(curpos, lift_speed)
        # Move the nozzle over the probe point
        x_offset, y_offset, z_offset = self.get_offsets()
        curpos[0] += x_offset
        curpos[1] += y_offset
        self._move(curpos, params['probe_speed'])
        # Start manual probe
        manual_probe.ManualProbeHelper(self.printer, gcmd,
                                       self.probe_calibrate_finalize)
    cmd_Z_OFFSET_APPLY_PROBE_help = "Adjust the probe's z_offset"

    def cmd_Z_OFFSET_APPLY_PROBE(self, gcmd):
        offset = self.gcode_move.get_status()['homing_origin'].z
        configfile = self.printer.lookup_object('configfile')
        if offset == 0:
            self.gcode.respond_info("Nothing to do: Z Offset is 0")
        else:
            new_calibrate = self.z_offset - offset
            self.gcode.respond_info(
                "%s: z_offset: %.3f\n"
                "The SAVE_CONFIG command will update the printer config file\n"
                "with the above and restart the printer."
                % (self.name, new_calibrate))
            configfile.set(self.name, 'z_offset', "%.3f" % (new_calibrate,))


# BDsensor wrapper that enables probe specific features
# set this type of sda_pin 2 as virtual endstop
# add new gcode command M102 for BDsensor
class BDsensorEndstopWrapper:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.printer.register_event_handler('klippy:mcu_identify', self._handle_mcu_identify)

        self.config = config
        self.name = config.get_name()
        self.g28_cmd = config.get('homing_cmd', 'G28')
        self.z_adjust = config.getfloat('z_adjust', 0., minval=-0.3, below=0.3)
        self.z_offset = config.getfloat('z_offset', 0., minval=-0.6, maxval=0.6)
        self.position_endstop = config.getfloat('position_endstop', 0.7,
                                                minval=0.5, below=2.5)
        if self.z_adjust > self.position_endstop:
            raise self.printer.command_error("The 'z_adjust' cannot be greater"
                                             " than 'position_endstop' in "
                                             "section [BDsensor]")
        if self.z_offset > self.position_endstop:
            raise self.printer.command_error("The 'z_offset' cannot be greater"
                                             " than 'position_endstop' in "
                                             "section [BDsensor]")
        self.stow_on_each_sample = config.getboolean(
            'deactivate_on_each_sample', True)
        self.no_stop_probe = config.get('no_stop_probe', None)
        self.collision_homing = config.getint('collision_homing', 0)
        self.collision_calibrate = config.getint('collision_calibrate', 0)
        self.rt_sample_time = config.getint('rt_sample_time', 0)#ms
        self.rt_max_range = config.getfloat('rt_max_range', 0, minval=0)
        self.QGL_Tilt_Probe = config.getint('QGL_Tilt_Probe', 1)
        self.switch_mode_sample_time = config.getfloat('SWITCH_MODE_SAMPLE_TIME', 0.006)
        self.speed = config.getfloat('speed', 3.0, above=0.)
        
        gcode_macro = self.printer.load_object(config,
                                               'gcode_macro')
        self.activate_gcode = \
            gcode_macro.load_template(config, 'activate_gcode', '')
        self.deactivate_gcode = \
            gcode_macro.load_template(config, 'deactivate_gcode', '')

        self.collision_calibrating = 0
        self.switch_mode = 0
        self.printer.register_event_handler("stepper_enable:motor_off",
                                            self.event_motor_off)                                    
        ppins = self.printer.lookup_object('pins')
        # self.mcu_pwm = ppins.setup_pin('pwm', config.get('scl_pin'))
        self.bdversion = ''
        # Command timing
        self.next_cmd_time = self.action_end_time = 0.
        pin = config.get('sda_pin')
        pin_params = ppins.lookup_pin(pin, can_invert=False, can_pullup=True)
        mcu = pin_params['chip']
        self.sda_pin_num = pin_params['pin']
        self.mcu = mcu
        # print("b2:%s"%mcu)
        pin_s = []
        try:
            pin_s = config.get('scl_pin')
        except Exception as e:
            try:
                pin_s = config.get('clk_pin')
            except Exception as e:
                raise self.printer.command_error("%s" % str(e))
            pass
        pin_params = ppins.lookup_pin(pin_s, can_invert=False, can_pullup=True)
        mcu = pin_params['chip']
        scl_pin_num = pin_params['pin']
        # print("b3:%s"%mcu)
        # pin_params['pullup']=2
        # self.mcu_endstop = mcu.setup_pin('endstop', pin_params)
        self._invert = pin_params['invert']

        self.oid = self.mcu.create_oid()

        self.mcu_endstop = self.mcu
        self._invert_endstop = self._invert
        self.oid_endstop = self.oid
        self.endstop_pin_num = self.sda_pin_num
        self.endstop_bdsensor_offset = 0
        try:
            pin = config.get('endstop_pin')
            pin_params = ppins.lookup_pin(pin, can_invert=True, can_pullup=True)
            self.endstop_pin_num = pin_params['pin']
            self.mcu_endstop = pin_params['chip']
            self._invert_endstop = pin_params['invert']
            if self.mcu_endstop is not self.mcu:
                self.oid_endstop = self.mcu_endstop.create_oid()
        except Exception as e:
            pass
        self.cmd_queue = self.mcu.alloc_command_queue()
        # Setup iterative solver
        ffi_main, ffi_lib = chelper.get_ffi()
        self.trapq = ffi_main.gc(ffi_lib.trapq_alloc(), ffi_lib.trapq_free)
        self.trapq_append = ffi_lib.trapq_append
        self.trapq_finalize_moves = ffi_lib.trapq_finalize_moves
        self.stepper_kinematics = ffi_main.gc(
            ffi_lib.cartesian_stepper_alloc(b'x'), ffi_lib.free)

        self.config_fmt = (
                "config_I2C_BD oid=%d sda_pin=%s scl_pin=%s delay=%s"
                " h_pos=%d z_adjust=%d estop_pin=%s"
                % (self.oid, self.sda_pin_num, scl_pin_num, config.get('delay'), self.position_endstop * 100, self.z_adjust * 100,self.endstop_pin_num))

        self.mcu.add_config_cmd(self.config_fmt)
        self.I2C_BD_send_cmd = None
        # MCU_BD_I2C_from_config(self.mcu,config)

        # Register commands
        self.gcode = self.printer.lookup_object('gcode')
        self.width_filament = config.getint('width_filament', 0)
        if self.width_filament == 0:
            self.gcode.register_command('M102', self.cmd_M102)
            self.gcode.register_command('BDSENSOR_VERSION', self.BD_version)
            self.gcode.register_command('BDSENSOR_CALIBRATE', self.BD_calibrate)
            self.gcode.register_command('BDSENSOR_READ_CALIBRATION', self.BD_read_calibration)
            self.gcode.register_command('BDSENSOR_DISTANCE', self.bd_distance)
            self.gcode.register_command('BDSENSOR_SET', self.bd_set)

        self.gcode_move = self.printer.load_object(config, "gcode_move")
        self.gcode = self.printer.lookup_object('gcode')
        self.bedmesh = self.printer.lookup_object('bed_mesh', None)
        # Wrappers
        self.bd_value = 10.24
        self.results = []
        self.finish_home_complete = self.wait_trigger_complete = None
        # multi probes state
        self.multi = 'OFF'
        self.mcu.register_config_callback(self.build_config)
        self.adjust_range = 0
        self.old_count = 1000
        self.homing = 0
        self.reactor = self.printer.get_reactor()
        self.bd_update_timer = self.reactor.register_timer(
            self.bd_update_event)
        
        self.status_dis = None
        # try:
        # self.status_dis=self.printer.lookup_object('display_status')
        # except Exception as e:
        #    pass
        self._rest_ticks = 0
        ffi_main, ffi_lib = chelper.get_ffi()
        self._trdispatch = ffi_main.gc(ffi_lib.trdispatch_alloc(), ffi_lib.free)
        self._trsyncs = [MCU_trsync(self.mcu_endstop, self._trdispatch)]
        self.ncont = 0
        self.z_last = 0
        self.z_offset_adj = 0

    def get_mcu(self):
        return self.mcu

    def build_config(self):
        cmd_queue = self._trsyncs[0].get_command_queue()
        self.I2C_BD_send_cmd = self.mcu.lookup_query_command(
            "I2CBD oid=%c c=%c d=%c",
            "I2CBDr oid=%c r=%c",
            oid=self.oid, cq=self.cmd_queue)

        self.mcu.register_response(self._handle_BD_Update,
                                   "BD_Update", self.oid)
        self.mcu.register_response(self.handle_probe_Update,
                                   "X_probe_Update", self.oid)
        self.mcu_endstop.add_config_cmd(
            "BDendstop_home oid=%d clock=0 sample_ticks=0 sample_count=0"
            " rest_ticks=0 pin_value=0 trsync_oid=0 trigger_reason=0"
            " endstop_pin=%s sw=0 sw_val=0"
            % (self.oid_endstop, self.endstop_pin_num), on_restart=True)
        # Lookup commands

        self._home_cmd = self.mcu_endstop.lookup_command(
            "BDendstop_home oid=%c clock=%u sample_ticks=%u sample_count=%c"
            " rest_ticks=%u pin_value=%c trsync_oid=%c trigger_reason=%c"
            " endstop_pin=%c sw=%c sw_val=%c",
            cq=cmd_queue
        )

    def I2C_BD_send(self, cmd, data=0):
        data = int(data)
        if cmd == CMD_READ_DATA or cmd == CMD_READ_ENDSTOP:
            pr = self.I2C_BD_send_cmd.send([self.oid, cmd, data])
            #self.gcode.respond_info(f"{pr}")
            return int(pr['r'])
        else:
            return self.I2C_BD_send_cmd.send([self.oid, cmd, data])

    def _handle_BD_Update(self, params):
        try:
            self.bd_value = int(params['distance_val']) / 100.00
            if self.status_dis is not None:
                strd = str(self.bd_value) + "mm"
                if self.bd_value == 10.24:
                    strd = "BDs:ConnectErr"
                if self.bd_value == 3.9:
                    strd = "BDs:Out Range"
                self.status_dis.message = strd
        except ValueError as e:
            pass

    def handle_probe_Update(self, params):
        count = int(params['distance_val'].split(b' ')[1])
        self.old_count = count
        try:
            self.results.append(int(params['distance_val'].split(b' ')[0]))
        except ValueError as e:
            pass

    def _force_enable(self, stepper):
        self.toolhead = self.printer.lookup_object('toolhead')
        print_time = self.toolhead.get_last_move_time()
        stepper_enable = self.printer.lookup_object('stepper_enable')
        enable = stepper_enable.lookup_enable(stepper.get_name())
        was_enable = enable.is_motor_enabled()
        STALL_TIME = 0.100
        if not was_enable:
            enable.motor_enable(print_time)
            self.toolhead.dwell(STALL_TIME)
        return was_enable

    def manual_move(self, stepper, dist, speed, accel=0.):
        self.toolhead = self.printer.lookup_object('toolhead')
        self.toolhead.flush_step_generation()
        prev_sk = stepper.set_stepper_kinematics(self.stepper_kinematics)
        prev_trapq = stepper.set_trapq(self.trapq)
        stepper.set_position((0., 0., 0.))
        axis_r, accel_t, cruise_t, cruise_v = calc_move_time(dist, speed, accel)
        print_time = self.toolhead.get_last_move_time()
        self.trapq_append(
            self.trapq, print_time, accel_t, cruise_t, accel_t,
            0., 0., 0., axis_r, 0., 0., 0., cruise_v, accel
        )
        print_time = print_time + accel_t + cruise_t + accel_t
        stepper.generate_steps(print_time)
        self.trapq_finalize_moves(self.trapq, print_time + 99999.9)
        stepper.set_trapq(prev_trapq)
        stepper.set_stepper_kinematics(prev_sk)
        self.toolhead.note_kinematic_activity(print_time)
        self.toolhead.dwell(accel_t + cruise_t + accel_t)

    def bd_set_cur_z(self, hgt, s_log):
        if s_log == 1:
            self.gcode.respond_info("Curent z:%.2f (gcode_z:%.2f - (z_offset:%.2f - z_offset_adj:%.2f))" % (hgt-(self.z_offset-self.z_offset_adj),hgt,self.z_offset,self.z_offset_adj))
        if (hgt-self.z_offset)<=0:
            self.gcode.respond_info("The real_time_adjust is ignored because current z:%.2f (gcode_z:%.2f - (z_offset:%.2f - z_offset_adj:%.2f)) <=0" % (hgt-(self.z_offset-self.z_offset_adj),hgt,self.z_offset,self.z_offset_adj))
            #self.gcode.respond_info("Since current z <0 ")
            return;
        hgt = hgt-(self.z_offset-self.z_offset_adj)
        hgt=int(hgt*1000) 
        self.I2C_BD_send(1026, hgt)
    def event_motor_off(self,print_time=0):
        if self.adjust_range != 0:
            self.BD_real_time(0)

    def bd_update_z(self,z):
        self.toolhead = self.printer.lookup_object('toolhead')
        kin = self.toolhead.get_kinematics()
        for stepper in kin.get_steppers():
            if stepper.is_active_axis('z') or stepper.is_active_axis('a') or stepper.is_active_axis('b') or stepper.is_active_axis('c'):
                self.bd_set_cur_z(z,1)   
                break
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        
    def bd_update_event(self, eventtime):
        z=self.gcode_move.last_position[2] - self.gcode_move.base_position[2]
        if self.homing == 1:
            self.reactor.update_timer(self.bd_update_timer, self.reactor.NEVER)

        gcode_move = self.printer.lookup_object("gcode_move")
        offset = gcode_move.get_status()['homing_origin'].z
        if self.z_offset_adj != offset and offset != 0:
            self.z_offset_adj = offset
            self.bd_update_z(z)

        if self.z_last != z  and self.homing == 0:
            self.z_last = z
            self.bd_update_z(z)
        return eventtime + BD_TIMER

    def cmd_M102(self, gcmd, wait=False):
        # self.gcode_que=gcmd
        self.process_M102(gcmd)

    def BD_Sensor_Read(self, fore_r):
        if fore_r > 0:
            self.I2C_BD_send(CMD_DISTANCE_MODE)  #     prepare to read distance data
        intr = self.I2C_BD_send(CMD_READ_DATA, 1) # read data
        if intr >= DATA_ERROR:
            intr = self.I2C_BD_send(CMD_READ_DATA, 1)
        self.bd_value = intr / 100.00
        if fore_r == 0:
            if self.bd_value >= 10.24:
                self.gcode.respond_info("Bed Distance Sensor "
                                                 "data error0:%.2f"
                                                 % self.bd_value)
                raise self.printer.command_error("Bed Distance Sensor "
                                                 "data error0:%.2f"
                                                 % self.bd_value)
            elif self.bd_value > 3.8:
                self.gcode.respond_info("Bed Distance Sensor, "
                                                 "out of range.:%.2f "
                                                 % self.bd_value)
                raise self.printer.command_error("Bed Distance Sensor, "
                                                 "out of range.:%.2f "
                                                 % self.bd_value)
        elif fore_r == 2:
            if self.bd_value >= 10.24:
                self.gcode.respond_info("Bed Distance Sensor "
                                                 "data error2:%.2f"
                                                 % self.bd_value)
                raise self.printer.command_error("Bed Distance Sensor "
                                                 "data error2:%.2f"
                                                 % self.bd_value)

        return self.bd_value + self.z_offset

    def BD_version(self, gcmd,r_lenght=20):
        self.I2C_BD_send(CMD_READ_VERSION)  # 1016 // // read sensor version
       # self.I2C_BD_send(CMD_READ_VERSION)
        self.toolhead = self.printer.lookup_object('toolhead')
        ncount1 = 0
        x = []
        while 1:
            intd = self.I2C_BD_send(CMD_READ_DATA, 1)
            if intd > 127:
                intd = 127
            if intd < 0x20:
                intd = 0x20
            x.append(intd)
            # self.toolhead.dwell(0.1)
            ncount1 = ncount1 + 1
            if ncount1 >= r_lenght:
                self.I2C_BD_send(CMD_DISTANCE_MODE)
                res = ''.join(map(chr, x))
                self.bdversion = res
                break
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        #self.I2C_BD_send(CMD_DISTANCE_MODE)
        self.switch_mode = 1
        if "V1.0 " in self.bdversion \
           or "V1.1 " in self.bdversion \
           or "V1.2 " in self.bdversion:
            self.switch_mode = 0
        if "V1." in self.bdversion:
            self.gcode.respond_info("BDsensorVer:%s,switch_mode=%d,"
                                    "collision_homing=%d,collision_cal=%d"
                                    % (self.bdversion, self.switch_mode,
                                       self.collision_homing,
                                       self.collision_calibrate))
        else:
            self.gcode.respond_info("No data or corrupt data from BDsensor(%s), "
                                    "Please check connection"%self.bdversion)

    def BD_calibrate(self, gcmd):
        if "V1." not in self.bdversion:
            self.BD_version(self.gcode,20)
        if self.switch_mode == 1 and self.collision_calibrate == 1:
            self.collision_calibrating = 1
            #gcmd.respond_info("Homing")
            self.gcode.run_script_from_command("BED_MESH_CLEAR")
            curtime = self.printer.get_reactor().monotonic()
            if 'x' not in self.toolhead.get_status(curtime)['homed_axes']:
                gstr=self.g28_cmd
                gcmd.respond_info("Homing all:"+gstr)
                self.gcode.run_script_from_command(gstr)
            elif 'z' not in self.toolhead.get_status(curtime)['homed_axes']:
                gstr=self.g28_cmd+" Z"
                gcmd.respond_info("Homing z:"+gstr)
                self.gcode.run_script_from_command(gstr)
            self.gcode.run_script_from_command("G1 Z0")
        self.toolhead.wait_moves()
        gcmd.respond_info("Calibrating, don't power off the printer")
        self.toolhead = self.printer.lookup_object('toolhead')
       # kin = self.toolhead.get_kinematics()
        self.I2C_BD_send(CMD_DISTANCE_RAWDATA_TYPE)
        self.I2C_BD_send(CMD_DISTANCE_RAWDATA_TYPE)
        raw_d = self.I2C_BD_send(CMD_READ_DATA, 1)
        if raw_d > 600 :
            if raw_d == DATA_ERROR:
                raise self.printer.command_error("Unable to communicate with bdsensor,%d"%raw_d)
            raise self.printer.command_error("BDsensor is too far from the bed:%d"%raw_d)
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        
        self.I2C_BD_send(CMD_START_CALIBRATE)
        self.I2C_BD_send(CMD_START_CALIBRATE)
        self.gcode.run_script_from_command("SET_KINEMATIC_POSITION Z=0")
        curtime = self.printer.get_reactor().monotonic()
        if 'z' not in self.toolhead.get_status(curtime)['homed_axes']:
            raise self.printer.command_error("make sure [force_move]"
                                            " enable_force_move: true # in the printer.cfg")
        self.toolhead.dwell(0.2)
        gcmd.respond_info("Please Wait... ")
        z_pos = 0
        ncount = 0
        while 1:
            z_pos += 0.1
            self.I2C_BD_send((ncount))
            self.I2C_BD_send((ncount))
            self.I2C_BD_send((ncount))
            self.I2C_BD_send((ncount))
            self.toolhead.dwell(0.2)
            # self.gcode.run_script_from_command("G91")
            # self.gcode.run_script_from_command("G1 Z+0.1 F1500")
            # self.gcode.run_script_from_command("G90")
            self.toolhead.manual_move([None, None, z_pos], 2)
            self.toolhead.wait_moves()
            self.toolhead.dwell(0.2)
            ncount = ncount + 1
            if ncount >= 40:
                self.I2C_BD_send(CMD_END_CALIBRATE)
                self.toolhead.dwell(1)
                self.I2C_BD_send(CMD_DISTANCE_MODE)
                self.I2C_BD_send(CMD_DISTANCE_MODE)
                self.toolhead.dwell(1)
                self.BD_read_calibration(gcmd)
                gcmd.respond_info("Calibrate Finished!")
                #gcmd.respond_info("You can send command "
                #                  "BDSENSOR_READ_CALIBRATION "
                #                  "to check the calibration data")
                self.z_adjust = 0
                #configfile = self.printer.lookup_object('configfile')
                #configfile.set(self.name, 'z_adjust', "0.0")
                break
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        self.collision_calibrating = 0
        #self.toolhead.dwell(1)
        #self.BD_read_calibration(gcmd)
    def BD_read_calibration(self, gcmd):
        self.I2C_BD_send(CMD_START_READ_CALIBRATE_DATA)  # tart read raw calibrate data
        self.I2C_BD_send(CMD_START_READ_CALIBRATE_DATA)
        self.toolhead = self.printer.lookup_object('toolhead')
        ncount1 = 0
        while 1:
            intd = self.I2C_BD_send(CMD_READ_DATA, 1)
            gcmd.respond_info("%d at %.1fmm"%(intd,ncount1/10.0))
            if ncount1 <= 3 and intd > 500:
                gcmd.respond_raw("BDSensor mounted too high!"
                                 "0.4mm to 2.4mm from BED at"
                                 "zero position is recommended")
                # break
            if intd < 45:
                gcmd.respond_raw("BDSensor mounted too close! please mount"
                                 "the BDsensor 0.2~0.4mm higher")
            # break
            self.toolhead.dwell(0.03)
            ncount1 = ncount1 + 1
            if ncount1 >= 40:
                break
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        self.I2C_BD_send(CMD_DISTANCE_MODE)

    def bd_distance(self, gcmd):
        self.bd_value = self.BD_Sensor_Read(1)
        strd = str(self.bd_value) + "mm"
        if self.bd_value == 10.24:
            strd = "BDsensor:Connection Error or not calibrated"
        elif self.bd_value >= 3.9:
            strd = "BDsensor:Out of measure Range or too close to the bed"
        gcmd.respond_raw(strd)
        try:
            self.status_dis = self.printer.lookup_object('display_status')
            self.status_dis.message = strd
        except Exception as e:
            pass
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        self.I2C_BD_send(CMD_DISTANCE_MODE)

    def bd_set(self, gcmd):
        cmd_bd = 0.0
        cmd_bd = gcmd.get_float('Z_ADJUST', None)
        if cmd_bd is not None:
            gcmd.respond_info("z_adjust:%f, recommend to move the nozzle "
                              "close to bed and calibrate again the BDsensor"
                              "instead of chang z_adjust" % cmd_bd)
            if cmd_bd >= 0.3:
                cmd_bd = 0.29
                gcmd.respond_info("it has been set to the max value 0.29")
            elif cmd_bd <= -0.3:
                cmd_bd = -0.29
                gcmd.respond_info("it has been set to the min value -0.29")
            self.z_adjust = cmd_bd
            configfile = self.printer.lookup_object('configfile')
            configfile.set(self.name, 'z_adjust', "%.3f" % cmd_bd)
            gcmd.respond_info("The SAVE_CONFIG command will update"
                              "the printer config")
            return

        cmd_bd = gcmd.get_float('REAL_TIME_HEIGHT', None)
        if cmd_bd is not None:
            self.BD_real_time(cmd_bd) 
            return

        cmd_bd = gcmd.get_int('NO_STOP_PROBE', None)
        if cmd_bd is not None:
            configfile = self.printer.lookup_object('configfile')
            configfile.set(self.name, 'no_stop_probe', "%d" % cmd_bd)
            gcmd.respond_info("no_stop_probe is setted:%d The SAVE_CONFIG"
                              "command will update the printer config", cmd_bd)
            return

        cmd_bd = gcmd.get_float('QGL_TILT_PROBE', None)
        if cmd_bd is not None:
            self.QGL_Tilt_Probe = cmd_bd
            self.gcode.respond_info("QGL_Tilt_Probe:%d"%self.QGL_Tilt_Probe)
            return

        cmd_bd = gcmd.get_float('COLLISION_HOMING', None)
        if cmd_bd is not None:
            self.collision_homing = cmd_bd
            return
        cmd_bd = gcmd.get_float('COLLISION_CALIBRATING', None)
        if cmd_bd is not None:
            self.collision_calibrating = cmd_bd
            return
        cmd_bd = gcmd.get_float('POSITION_ENDSTOP', None)
        if cmd_bd is not None:
            self.position_endstop = cmd_bd
            return

    def BD_real_time(self, bd_height):
        if bd_height >= 3.0:
            bd_height = 3
        elif bd_height < 0.0:
            bd_height = 0
        self.gcode.respond_info("Real time leveling height:%.2f ( %.2f - z_offset:%.2f ) "%(bd_height-self.z_offset,bd_height,self.z_offset))
        
        if bd_height == 0:
            self.adjust_range = 0
        else:
            bd_height = bd_height-self.z_offset
            self.adjust_range = int((bd_height+0.01)*1000)
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        self.toolhead = self.printer.lookup_object('toolhead')
        kin = self.toolhead.get_kinematics()
        z_index = 0
        for stepper in kin.get_steppers():
            if stepper.is_active_axis('z') or stepper.is_active_axis('a') or stepper.is_active_axis('b') or stepper.is_active_axis('c'):
                steps_per_mm = 1.0/stepper.get_step_dist()
                #z = self.gcode_move.last_position[2]
                z=self.gcode_move.last_position[2] - self.gcode_move.base_position[2]
                #stepper._query_mcu_position()
                invert_dir, orig_invert_dir = stepper.get_dir_inverted()
                #z = int(z*1000)
                self.I2C_BD_send(1025, z_index)
                #self.I2C_BD_send(1026, z)
                self.bd_set_cur_z(z,0)
                self.I2C_BD_send(1027, self.adjust_range)
                self.I2C_BD_send(1028, orig_invert_dir)
                self.I2C_BD_send(1029, steps_per_mm)
                if self.rt_sample_time > 0:
                    self.I2C_BD_send(1031, self.rt_sample_time)
                if self.rt_max_range > 0:
                    self.I2C_BD_send(1032, self.rt_max_range*1000)
                self.I2C_BD_send(1030, stepper.get_oid())
                z_index = z_index + 1
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        if self.adjust_range == 0:
            self.reactor.update_timer(self.bd_update_timer, self.reactor.NEVER)
        else:
            self.reactor.update_timer(self.bd_update_timer, self.reactor.NOW)
        
    def process_M102(self, gcmd):
        self.process_m102 = 1
        cmd_bd = 0
        try:
            cmd_bd = gcmd.get_int('S', None)
        except Exception as e:
            pass
            return
        self.toolhead = self.printer.lookup_object('toolhead')
        if cmd_bd == -6:
            self.BD_calibrate(gcmd)
        elif cmd_bd == -5:
            self.BD_read_calibration(gcmd)
        elif cmd_bd == -1:
            self.BD_version(gcmd,20)
        elif cmd_bd == -2:  # gcode M102 S-2 read distance data
            self.bd_distance(gcmd)
        elif cmd_bd == -7:
            self.I2C_BD_send(CMD_DISTANCE_RAWDATA_TYPE) 
            strd = "Raw data:" + str(self.I2C_BD_send(CMD_READ_DATA, 1))
            self.I2C_BD_send(CMD_DISTANCE_MODE)
            self.bd_value = self.BD_Sensor_Read(1)
            strd = strd + ",  Then we can calculate the distance by comparing to the calibration data:"+str(self.bd_value) + "mm"
            gcmd.respond_raw(strd) 
            return
        elif cmd_bd == -8:
            self.I2C_BD_send(CMD_REBOOT_SENSOR)
        elif cmd_bd == -9:
            self.I2C_BD_send(CMD_SWITCH_MODE)
            self.I2C_BD_send(str(int(self.position_endstop * 100)))
            self.gcode.respond_info("in switch mode, the endstop position is"
                                    "%.3f mm" % self.position_endstop)
            return
        else:
            return
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        # self.process_m102=0

    def _handle_mcu_identify(self):
        kin = self.printer.lookup_object('toolhead').get_kinematics()
        for stepper in kin.get_steppers():
            if stepper.is_active_axis('z'):
                self.add_stepper(stepper)

    def raise_probe(self):
        return

    def lower_probe(self):
        return

    def query_endstop(self, print_time=0):
        params = 1
        params = self.I2C_BD_send(CMD_READ_ENDSTOP)
        if self.endstop_pin_num != self.sda_pin_num and self._invert_endstop:
            if params == 0:
                params = 1
            else :
                 params = 0
        self.gcode.respond_info("params:%d" % params)
        return params

    def add_stepper(self, stepper):
        trsyncs = {trsync.get_mcu(): trsync for trsync in self._trsyncs}
        trsync = trsyncs.get(stepper.get_mcu())
        if trsync is None:
            trsync = MCU_trsync(stepper.get_mcu(), self._trdispatch)
            self._trsyncs.append(trsync)
        trsync.add_stepper(stepper)
        # Check for unsupported multi-mcu shared stepper rails
        sname = stepper.get_name()
        if sname.startswith('stepper_'):
            for ot in self._trsyncs:
                for s in ot.get_steppers():
                    if ot is not trsync and s.get_name().startswith(sname[:9]):
                        cerror = self.mcu.get_printer().config_error
                        raise cerror("Multi-mcu homing not supported on"
                                     " multi-mcu shared axis")

    def get_steppers(self):
        return [s for trsync in self._trsyncs for s in trsync.get_steppers()]

    def home_start(self, print_time, sample_time, sample_count, rest_time,
                   triggered=True):
        collision_value = int(self.position_endstop * 100)
        if "V1." not in self.bdversion:
            self.BD_version(self.gcode,5)
        #self.homing = 1
        if self.switch_mode == 1:
            #self.I2C_BD_send(CMD_SWITCH_MODE)
            sample_time = self.switch_mode_sample_time
            sample_count = 2
            if self.homing == 1 and (self.collision_homing == 1
               or self.collision_calibrating == 1):
                #self.I2C_BD_send(1)
                collision_value = 1
            #else:
           #     self.I2C_BD_send(int(self.position_endstop * 100))
                #self.gcode.respond_info("home_pos:%s" % str(int(self.position_endstop * 100)))
            # time.sleep(0.01)
        else:
            sample_time = .03
            sample_count = 1

        clock = self.mcu_endstop.print_time_to_clock(print_time)
        rest_ticks = \
            self.mcu_endstop.print_time_to_clock(print_time+rest_time) - clock
        self._rest_ticks = rest_ticks
        reactor = self.mcu_endstop.get_printer().get_reactor()
        self.wait_trigger_complete = \
            reactor.register_callback(self.wait_for_trigger)
        self.trigger_completion = reactor.completion()
        expire_timeout = TRSYNC_TIMEOUT
        if len(self._trsyncs) == 1:
            expire_timeout = TRSYNC_SINGLE_MCU_TIMEOUT
        for i, trsync in enumerate(self._trsyncs):
            report_offset = float(i) / len(self._trsyncs)
            trsync.start(print_time, report_offset,
                         self.trigger_completion, expire_timeout)
        self.etrsync = self._trsyncs[0]
        ffi_main, ffi_lib = chelper.get_ffi()
        ffi_lib.trdispatch_start(self._trdispatch,
                                 self.etrsync.REASON_HOST_REQUEST)

        self._home_cmd.send(
            [
                self.oid_endstop,
                clock,
                self.mcu_endstop.seconds_to_clock(sample_time),
                sample_count,
                self.mcu_endstop.seconds_to_clock(sample_time),
                triggered ^ self._invert_endstop,
                self.etrsync.get_oid(),
                self.etrsync.REASON_ENDSTOP_HIT,
                self.endstop_pin_num,
                self.switch_mode,collision_value
            ],
            reqclock=clock
        )
        self.finish_home_complete = self.trigger_completion
        return self.trigger_completion

    def wait_for_trigger(self, eventtime):
        self.trigger_completion.wait()
        if self.multi == 'OFF':
            self.raise_probe()

    def home_wait(self, home_end_time):
        etrsync = self._trsyncs[0]
        etrsync.set_home_end_time(home_end_time)
        if self.mcu_endstop.is_fileoutput():
            self.trigger_completion.complete(True)
        self.trigger_completion.wait()
        self._home_cmd.send([self.oid_endstop, 0, 0, 0, 0, 0, 0, 0,
                            self.endstop_pin_num,0,0])
        ffi_main, ffi_lib = chelper.get_ffi()
        ffi_lib.trdispatch_stop(self._trdispatch)
        res = [trsync.stop() for trsync in self._trsyncs]
        if any([r == etrsync.REASON_COMMS_TIMEOUT for r in res]):
            return -1.
        if res[0] != etrsync.REASON_ENDSTOP_HIT:
            return 0.
        if self.mcu_endstop.is_fileoutput():
            return home_end_time
        return home_end_time

    def multi_probe_begin(self):
       # self.BD_Sensor_Read(2)# check the if the BDsensor is working
        curtime = self.printer.get_reactor().monotonic()
        self.toolhead = self.printer.lookup_object('toolhead')
        kin_status = self.toolhead.get_kinematics().get_status(curtime)
        if 'z' not in kin_status['homed_axes'] and self.endstop_pin_num == self.sda_pin_num:
            #self.gcode.respond_info("Check bd sensor")
            self.BD_Sensor_Read(2)# check the if the BDsensor is working
        if self.stow_on_each_sample:
            return
        self.multi = 'FIRST'

    def adjust_probe_up(self, up_steps, second_steps, logd):
        homepos = self.toolhead.get_position()
        time.sleep(0.03)
        intr = self.I2C_BD_send(CMD_READ_DATA, 1)
        intr_old = intr
        if intr > 720:
            self.gcode.respond_info("warning: triggered in air, %d"%intr)
        pos_old = homepos[2]
        while 1:
            homepos[2] += up_steps
            self.toolhead.manual_move([None, None, homepos[2]], 2)
            self.toolhead.wait_moves()
            time.sleep(0.05)
            raw_d = self.I2C_BD_send(CMD_READ_DATA, 1)
            if (raw_d - intr) >= (up_steps*100*2):
                if second_steps == 0:
                    break
                pos_old_1 = homepos[2]
                homepos[2] -= 0.1 # (up_steps*1.5)
                self.toolhead.manual_move([None, None, homepos[2]], 2)
                self.toolhead.wait_moves()
                intr = raw_d
                while 1:
                    homepos[2] += second_steps
                    self.toolhead.manual_move([None, None, homepos[2]], 1)
                    self.toolhead.wait_moves()
                    time.sleep(0.05)
                    raw_d = self.I2C_BD_send(CMD_READ_DATA, 1)
                    #homepos_n = self.toolhead.get_position()
                    if (raw_d - intr) >= (second_steps*100*2) or homepos[2] >= pos_old_1:
                        if logd == 1:
                            temp = 0
                            try:
                                pheaters = self.printer.lookup_object('heaters')
                                heaters = pheaters.lookup_heater('heater_bed')
                                temp, target = heaters.get_temp(self.printer.get_reactor().monotonic())
                            except Exception as e:
                                pass
                            self.gcode.respond_info("Raw data:%d at 0 mm, BDsensor to bed: %.4f mm, Bed: %.1fC, z_offset: %.3f"
                                                % (raw_d,raw_d*0.004,temp,self.z_offset))
                        return homepos[2]-pos_old,raw_d-intr_old
                        break
                    intr = raw_d
                break
            intr = raw_d         
        return 0,0

    def adjust_probe_down(self, down_steps):
        homepos = self.toolhead.get_position()
        raw_d = self.I2C_BD_send(CMD_READ_DATA, 1)
        intr = raw_d
        homepos[2] = 5
        self.toolhead.set_position(homepos)
        while 1:
            homepos[2] -= down_steps
            self.toolhead.manual_move([None, None, homepos[2]], 2)
            self.toolhead.wait_moves()
            time.sleep(0.05)
            raw_d = self.I2C_BD_send(CMD_READ_DATA, 1)
            #self.gcode.respond_info("  %d "%raw_d)
            if (intr - raw_d) < down_steps*200 and raw_d < 600:
                #self.gcode.respond_info(" stop at %d "%raw_d)
                break;
            intr = raw_d


    

    def adjust_probe(self):
        self.toolhead = self.printer.lookup_object('toolhead')
        homepos = self.toolhead.get_position()
        self.I2C_BD_send(CMD_DISTANCE_RAWDATA_TYPE)
        self.I2C_BD_send(CMD_DISTANCE_RAWDATA_TYPE)
        adj_z,adj_raw = self.adjust_probe_up(0.1,0,1)      
        #if adj_z <= 0.15: # and adj_raw >= 6:
        
        self.adjust_probe_down(0.1)
        adj_z,adj_raw = self.adjust_probe_up(0.05,0.01,0) 
        
        self.adjust_probe_down(0.05)
        adj_z,adj_raw = self.adjust_probe_up(0.05,0.005,1) 
        
        #if adj_z <= 0.1: # and adj_raw >= 6:
        #    self.gcode.respond_info("re-adjusting")
        #    self.adjust_probe_down(0.1)
        #    adj_z,adj_raw = self.adjust_probe_up(0.05,0.01,1) 
        self.bd_value = self.BD_Sensor_Read(2)
        self.I2C_BD_send(CMD_DISTANCE_MODE)

    def multi_probe_end(self):
        self.toolhead = self.printer.lookup_object('toolhead')
        homepos = self.toolhead.get_position()        
        if self.endstop_pin_num == self.sda_pin_num:
           
            if self.switch_mode == 1 \
               and self.homing == 1 \
               and (self.collision_homing == 1
                    or self.collision_calibrating == 1):
                   
                self.adjust_probe()
                #homepos[2] = 0
                if self.collision_calibrating != 1:
                  #  homepos[2] = 0 + self.z_offset + 0.5
                    self.I2C_BD_send(CMD_DISTANCE_MODE)               
                    homepos = self.toolhead.get_position()
                    homepos[2] +=0.5               
                    self.toolhead.manual_move([None, None, homepos[2]], 2)
                    self.toolhead.wait_moves()
                    self.bd_value = self.BD_Sensor_Read(2)
                    time.sleep(0.1)
                    homepos[2] -=0.5
                    self.toolhead.manual_move([None, None, homepos[2]], 2)
                    self.toolhead.wait_moves()
                    self.gcode.respond_info("bd_value at %.3f mm" % self.bd_value)
                    homepos[2] = 0 + self.z_offset
                    if abs(self.bd_value-self.z_offset-0.5)>0.05:
                        self.gcode.respond_info("Detect the plate changed or others, the BD sensor needs recalibration %.3f" % (self.bd_value-0.5))
                       # self.toolhead.manual_move([None, None, homepos[2]+10], 2)
                        self.gcode.run_script_from_command("SET_KINEMATIC_POSITION Z=0")
                        self.gcode.run_script_from_command("M102 S-6")
                        
                        self.homing = 0
                        return
                    
                    
                else:
                    homepos[2] = 0
                self.toolhead.set_position(homepos)
            elif self.homing == 1:
                self.I2C_BD_send(CMD_DISTANCE_MODE)
                time.sleep(0.1)
                #self.gcode.respond_info("multi_probe_end")
                self.bd_value = self.BD_Sensor_Read(2)
                if self.bd_value > (self.position_endstop + 2):
                    self.gcode.respond_info("triggered at %.3f mm" % self.bd_value)
                    self.I2C_BD_send(CMD_REBOOT_SENSOR)
                    time.sleep(0.9)
                    self.bd_value = self.BD_Sensor_Read(2)
                    if self.bd_value > (self.position_endstop + 0.7):
                        raise self.printer.command_error("Home z failed! "
                                                         "the triggered at "
                                                         "%.3fmm" % self.bd_value)
                if self.bd_value <= 0:
                    self.gcode.respond_info("warning:triggered at 0mm")
                # time.sleep(0.1)
                self.endstop_bdsensor_offset = 0
                if self.sda_pin_num is not self.endstop_pin_num:
                    self.endstop_bdsensor_offset = homepos[2] - self.bd_value
                    self.gcode.respond_info("offset of endstop to bdsensor %.3fmm"
                                            % self.endstop_bdsensor_offset)
                else:
                    homepos[2] = self.bd_value
                    self.toolhead.set_position(homepos)
                # time.sleep(0.1)
                #self.gcode.respond_info("Z triggered at %.3f mm,auto adjusted."
                #                        % self.bd_value)
        self.homing = 0
        if self.stow_on_each_sample:
            return
        self.raise_probe()
        self.multi = 'OFF'

    def probe_prepare(self, hmove):
        if self.multi == 'OFF' or self.multi == 'FIRST':
            self.lower_probe()
            if self.multi == 'FIRST':
                self.multi = 'ON'

    def probe_finish(self, hmove):
        self.I2C_BD_send(CMD_DISTANCE_MODE)
        if self.multi == 'OFF':
            self.raise_probe()

    def get_position_endstop(self):
        # print("BD get_position_endstop")
        return self.position_endstop


def load_config(config):
    bdl = BDsensorEndstopWrapper(config)
    config.get_printer().add_object('probe', BDPrinterProbe(config, bdl))
    return bdl
