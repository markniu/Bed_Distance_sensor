# Bed leveling sensor BDsensor(Bed Distance sensor)
# https://github.com/markniu/Bed_Distance_sensor
# Copyright (C) 2023 Mark yue <niujl123@sina.com>
# This file may be distributed under the terms of the GNU GPLv3 license.
import sched, time
from threading import Timer

import chelper
import mcu,math
from . import probe
BD_TIMER = 0.500


# Calculate a move's accel_t, cruise_t, and cruise_v
def calc_move_time(dist, speed, accel):
    axis_r = 1.
    if dist < 0.:
        axis_r = -1.
        dist = -dist
    if not accel or not dist:
        return axis_r, 0., dist / speed, speed
    max_cruise_v2 = dist * accel
    if max_cruise_v2 < speed**2:
        speed = math.sqrt(max_cruise_v2)
    accel_t = speed / accel
    accel_decel_d = accel_t * speed
    cruise_t = (dist - accel_decel_d) / speed
    return axis_r, accel_t, cruise_t, speed


# I2C BD_SENSOR
# devices connected to an MCU via an virtual i2c bus(2 any gpio)

class MCU_I2C_BD:
    def __init__(self,mcu,   sda_pin,scl_pin, delay_t):
        self.mcu = mcu
        self.oid = self.mcu.create_oid()
        # Generate I2C bus config message
        self.config_fmt = (
            "config_I2C_BD oid=%d sda_pin=%s scl_pin=%s delay=%s"
            % (self.oid, sda_pin,scl_pin, delay_t))
        self.cmd_queue = mcu.alloc_command_queue()
        mcu.register_config_callback(self.build_config)
        self.I2C_BD_send_cmd = self.I2C_BD_receive_cmd = None
    def build_config(self):
        self.mcu.add_config_cmd(self.config_fmt)
        print ("MCU_I2C_BD %d" % self.oid)
        self.I2C_BD_send_cmd = self.mcu.lookup_command(
            "I2C_BD_send oid=%c data=%*s", cq=self.cmd_queue)
        self.I2C_BD_receive_cmd = self.mcu.lookup_query_command(
            "I2C_BD_receive oid=%c data=%*s",
            "I2C_BD_receive_response oid=%c response=%*s",
             oid=self.oid, cq=self.cmd_queue)

    def get_oid(self):
        return self.oid
    def get_mcu(self):
        return self.mcu
    def get_command_queue(self):
        return self.cmd_queue
    def I2C_BD_send(self, data):
        self.I2C_BD_send_cmd.send([self.oid, data.encode('utf-8')])
    def I2C_BD_receive(self,  data):
        return self.I2C_BD_receive_cmd.send([self.oid, data])

def MCU_BD_I2C_from_config(mcu,config):
    # Determine pin from config
    ppins = config.get_printer().lookup_object("pins")
    pin_sda=config.get('sda_pin')
    pin_scl=config.get('scl_pin')
    return MCU_I2C_BD(mcu,pin_sda,pin_scl,config.get('delay'))

# BDsensor wrapper that enables probe specific features
# set this type of sda_pin 2 as virtual endstop
# add new gcode command M102 for BDsensor
class BDsensorEndstopWrapper:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.config = config
        self.position_endstop = config.getfloat('z_offset')
        self.stow_on_each_sample = config.getboolean(
            'deactivate_on_each_sample', True)
        gcode_macro = self.printer.load_object(config, 'gcode_macro')
        self.activate_gcode = gcode_macro.load_template(
            config, 'activate_gcode', '')
        self.deactivate_gcode = gcode_macro.load_template(
            config, 'deactivate_gcode', '')
        # Create an "endstop" object to handle the probe pin
        ppins = self.printer.lookup_object('pins')
        pin = config.get('sda_pin')
        pin_params = ppins.lookup_pin(pin, can_invert=True, can_pullup=True)
        self.mcu = mcu.get_printer_mcu(self.printer, 'mcu')
        # set this type of sda_pin 2 as virtual endstop
        pin_params['pullup']=2
        self.mcu_endstop = self.mcu.setup_pin('endstop', pin_params)
        self.printer.register_event_handler('klippy:mcu_identify',
                                            self._handle_mcu_identify)
        self.oid = self.mcu.create_oid()
        self.cmd_queue = self.mcu.alloc_command_queue()
        # Setup iterative solver
        ffi_main, ffi_lib = chelper.get_ffi()
        self.trapq = ffi_main.gc(ffi_lib.trapq_alloc(), ffi_lib.trapq_free)
        self.trapq_append = ffi_lib.trapq_append
        self.trapq_finalize_moves = ffi_lib.trapq_finalize_moves
        self.stepper_kinematics = ffi_main.gc(
            ffi_lib.cartesian_stepper_alloc(b'x'), ffi_lib.free)

        self.bd_sensor=MCU_BD_I2C_from_config(self.mcu,config)
        self.distance=5;
        # Register M102 commands
        self.gcode = self.printer.lookup_object('gcode')
        self.gcode.register_command('M102', self.cmd_M102)
        self.no_stop_probe = config.get('no_stop_probe', None)
        print(self.no_stop_probe)
        self.I2C_BD_receive_cmd2 = None
        self.gcode_move = self.printer.load_object(config, "gcode_move")
        self.gcode = self.printer.lookup_object('gcode')
        # Wrappers
        self.get_mcu = self.mcu_endstop.get_mcu
        self.add_stepper = self.mcu_endstop.add_stepper
        self.get_steppers = self.mcu_endstop.get_steppers
        #self.home_start = self.mcu_endstop.home_start
        self.home_wait = self.mcu_endstop.home_wait
        self.query_endstop = self.mcu_endstop.query_endstop
        self.process_m102=0
        self.gcode_que=None
        self.zl=0
        self.bd_value=10.24
        self.x_offset = config.getfloat('x_offset', 0.)
        self.y_offset = config.getfloat('y_offset', 0.)
        self.results = []
        self.finish_home_complete = self.wait_trigger_complete = None
        # multi probes state
        self.multi = 'OFF'
        self.mcu.register_config_callback(self.build_config)
        self.adjust_range=0;
        self.old_count=1000;
        #bd_scheduler = sched.scheduler(time.time, time.sleep)
        #bd_scheduler.enter(1, 1, self.BD_loop, (bd_scheduler,))
        #bd_scheduler.run()
        #Timer(2, self.BD_loop, ()).start()
        self.reactor = self.printer.get_reactor()
        self.bd_update_timer = self.reactor.register_timer(
            self.bd_update_event)
        self.reactor.update_timer(self.bd_update_timer, self.reactor.NOW)
        print(config.getfloat('speed', 5.))

    def z_live_adjust(self):
        print ("z_live_adjust %d" % self.adjust_range)
        if self.adjust_range<=0 or self.adjust_range > 40:
            return
        self.toolhead = self.printer.lookup_object('toolhead')
        phoming = self.printer.lookup_object('homing')
       # print ("homing_status %d" % phoming.homing_status)
       # if phoming.homing_status == 1:
        #    return
        #x, y, z, e = self.toolhead.get_position()
        z=self.gcode_move.last_position[2]
        print ("z %.4f" % z)
        if z is None:
            return
        if z*10>self.adjust_range:
            return
        if self.bd_value < 0:
            return
        if self.bd_value>10.15:
            return;
        if abs(z-self.bd_value)<=0.01:
            return;
        print ("z_post:%.4f" % z)
        print ("bd_value:%.4f" % self.bd_value)
        #self.toolhead.wait_moves()
        kin = self.toolhead.get_kinematics()
        distance = 0.5#gcmd.get_float('DISTANCE')
        speed = 7#gcmd.get_float('VELOCITY', above=0.)
        accel = 2000#gcmd.get_float('ACCEL', 0., minval=0.)
        ajust_len=-0.01
        #if z > self.bd_value:
        #    ajust_len = (z-self.bd_value)
        #else:
        #ajust_len = z+z-self.bd_value
        self.zl= self.zl+0.1
        ajust_len =  z-self.bd_value
        print ("ajust_len:%.4f" % ajust_len)
        #if ajust_len<0:
        #    return
        dir=1
        if ajust_len>0.000001:
            dir=0
        delay=1000000
        steps_per_mm = 1.0/stepper.get_step_dist()
        for stepper in kin.get_steppers():
            if stepper.is_active_axis('z'):
                cmd_fmt = (
                "%u %u %u %u\0"
                % (dir,steps_per_mm, delay,stepper.get_oid()))
                pr=self.Z_Move_Live_cmd.send([self.oid,
                    cmd_fmt.encode('utf-8')])
                print("get:%s " %pr['return_set'])
           #self.toolhead.manual_move([None, None, ajust_len], speed)

    def bd_update_event(self, eventtime):
        if self.gcode_que is not None:
            self.process_M102(self.gcode_que)
            self.gcode_que=None
        strd=str(self.bd_value)+"mm"
        try:
            status_dis=self.printer.lookup_object('display_status')
            if status_dis is not None:
                if self.bd_value == 10.24:
                    strd="BDs:ConnectErr"
                if self.bd_value == 3.9:
                    strd="BDs:Out Range"
                status_dis.message=strd
        except Exception as e:
            pass
        #self.z_live_adjust()
        return eventtime + BD_TIMER

    def build_config(self):
        self.I2C_BD_receive_cmd = self.mcu.lookup_query_command(
            "I2C_BD_receive oid=%c data=%*s",
            "I2C_BD_receive_response oid=%c response=%*s",
            oid=self.oid, cq=self.cmd_queue)
        #self.I2C_BD_receive_cmd2 = self.mcu.lookup_query_command(
        #    "I2C_BD_receive2 oid=%c data=%*s",
        #    "I2C_BD_receive2_response oid=%c response=%*s",
         #   oid=self.oid, cq=self.cmd_queue)

        self.Z_Move_Live_cmd = self.mcu.lookup_query_command(
            "Z_Move_Live oid=%c data=%*s",
            "Z_Move_Live_response oid=%c return_set=%*s",
            oid=self.oid, cq=self.cmd_queue)
        self.mcu.register_response(self._handle_BD_Update,
                                    "BD_Update", self.bd_sensor.oid)
        self.mcu.register_response(self._handle_probe_Update,
                                    "X_probe_Update", self.bd_sensor.oid)
    def _handle_BD_Update(self, params):
        #print("_handle_BD_Update :%s " %params['distance_val'])
        try:
            self.bd_value=int(params['distance_val'])/100.00
        except ValueError as e:
            pass
        #else:
            #print (" handle self.bd_value %.4f" % self.bd_value)
    def _handle_probe_Update(self, params):
        print("_handle_probe_Update:%s " %params['distance_val'])
        #print ("split :%s " %params['distance_val'].split(b' '))
        count=int(params['distance_val'].split(b' ')[1])
        print(len(self.results))

        self.old_count=count
        print ("split:%s " %params['distance_val'].split(b' '))
        try:
            self.results.append(int(params['distance_val'].split(b' ')[0]))
        except ValueError as e:
            pass
    def manual_move2(self, stepper, dist, speed, accel=0.):
         self.toolhead = self.printer.lookup_object('toolhead')
         self.toolhead.flush_step_generation()
         prev_sk = stepper.set_stepper_kinematics(self.stepper_kinematics)
         prev_trapq = stepper.set_trapq(self.trapq)
         stepper.set_position((0., 0., 0.))
         axis_r, accel_t,cruise_t,cruise_v=calc_move_time(dist, speed, accel)
         print_time = self.toolhead.get_last_move_time()
         self.trapq_append(self.trapq, print_time, accel_t, cruise_t, accel_t,
                           0., 0., 0., axis_r, 0., 0., 0., cruise_v, accel)
         print_time = print_time + accel_t + cruise_t + accel_t
         stepper.generate_steps(print_time)
         self.trapq_finalize_moves(self.trapq, print_time + 99999.9)
         stepper.set_trapq(prev_trapq)
         stepper.set_stepper_kinematics(prev_sk)
         self.toolhead.note_kinematic_activity(print_time)
         #self.toolhead.dwell(accel_t + cruise_t + accel_t)

    def _force_enable(self,stepper):
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
         axis_r, accel_t,cruise_t,cruise_v=calc_move_time(dist, speed, accel)
         print_time = self.toolhead.get_last_move_time()
         self.trapq_append(self.trapq, print_time, accel_t, cruise_t, accel_t,
                           0., 0., 0., axis_r, 0., 0., 0., cruise_v, accel)
         print_time = print_time + accel_t + cruise_t + accel_t
         stepper.generate_steps(print_time)
         self.trapq_finalize_moves(self.trapq, print_time + 99999.9)
         stepper.set_trapq(prev_trapq)
         stepper.set_stepper_kinematics(prev_sk)
         self.toolhead.note_kinematic_activity(print_time)
         self.toolhead.dwell(accel_t + cruise_t + accel_t)

    def cmd_M102(self, gcmd, wait=False):
         self.gcode_que=gcmd
    def process_M102(self, gcmd):
        self.process_m102=1
        #self.reactor.update_timer(self.bd_update_timer, self.reactor.NOW)
        #self.reactor.update_timer(self.bd_update_timer, self.reactor.NOW)
        try:
            CMD_BD = gcmd.get_int('S', None)
        except AttributeError as e:
            return
        self.toolhead = self.printer.lookup_object('toolhead')
        if CMD_BD == -6:
            print("process_M102 0")
            kin = self.toolhead.get_kinematics()
            self.bd_sensor.I2C_BD_send("1019")
            distance = 0.5#gcmd.get_float('DISTANCE')
            speed = 10#gcmd.get_float('VELOCITY', above=0.)
            accel = 2000#gcmd.get_float('ACCEL', 0., minval=0.)
            self.distance=0.1
            for stepper in kin.get_steppers():
                #if stepper.is_active_axis('z'):
                self._force_enable(stepper)
                self.toolhead.wait_moves()
            ncount=0
            print("process_M102 1")
            while 1:
                self.bd_sensor.I2C_BD_send(str(ncount))
                self.bd_sensor.I2C_BD_send(str(ncount))
                self.bd_sensor.I2C_BD_send(str(ncount))
                self.toolhead.dwell(0.2)
                for stepper in kin.get_steppers():
                    if stepper.is_active_axis('z'):
                        self._force_enable(stepper)
                        self.manual_move(stepper, self.distance, speed)
                        print("process_M102 2")
                self.toolhead.wait_moves()
                ncount=ncount+1
                if ncount>=40:
                    self.bd_sensor.I2C_BD_send("1021")
                    break
        elif  CMD_BD == -5:
            self.bd_sensor.I2C_BD_send("1017")#tart read raw calibrate data
            ncount1=0
            while 1:
                pr=self.I2C_BD_receive_cmd.send([self.oid,"3".encode('utf-8')])
                intd=int(pr['response'])
                strd=str(intd)
                gcmd.respond_raw(strd)
                self.toolhead.dwell(0.1)
                ncount1=ncount1+1
                if ncount1>=40:
                    break
        elif  CMD_BD == -1:
            self.bd_sensor.I2C_BD_send("1016")#1016 // // read sensor version
            ncount1=0
            x=[]
            while 1:
                pr=self.I2C_BD_receive_cmd.send([self.oid,"3".encode('utf-8')])
              #  print"params:%s" % pr['response']
                intd=int(pr['response'])
                if intd>127:
                    intd=127
                if intd<0x20:
                    intd=0x20
                x.append(intd)
                self.toolhead.dwell(0.3)
                ncount1=ncount1+1
                if ncount1>=20:
                    self.bd_sensor.I2C_BD_send("1018")#1018// finish reading
                    res = ''.join(map(chr, x))
                    gcmd.respond_raw(res)
                    break
        elif  CMD_BD == -2:# gcode M102 S-2 read distance data
            self.bd_sensor.I2C_BD_send("1015")#1015   read distance data
            pr = self.I2C_BD_receive_cmd.send([self.oid, "32".encode('utf-8')])
            self.bd_value=int(pr['response'])/100.00
            strd=str(self.bd_value)+"mm"
            if self.bd_value == 10.24:
                strd="BDsensor:Connection Error"
            if self.bd_value == 3.9:
                strd="BDsensor:Out of measure Range"
            gcmd.respond_raw(strd)
        elif  CMD_BD ==-7:# gcode M102 Sx
            #self.bd_sensor.I2C_BD_send("1022")
            step_time=100
            self.toolhead = self.printer.lookup_object('toolhead')
            bedmesh = self.printer.lookup_object('bed_mesh', None)
            self.min_x, self.min_y = bedmesh.bmc.orig_config['mesh_min']
            self.max_x, self.max_y = bedmesh.bmc.orig_config['mesh_max']
            x_count=bedmesh.bmc.orig_config['x_count']
            kin = self.toolhead.get_kinematics()
            for stepper in kin.get_steppers():
                if stepper.get_name()=='stepper_x':
                    steps_per_mm = 1.0/stepper.get_step_dist()
                    x=self.gcode_move.last_position[0]
                    stepper._query_mcu_position()
                    invert_dir, orig_invert_dir = stepper.get_dir_inverted()
                    print("invert_dir:%d,%d" % (invert_dir,orig_invert_dir))
                    print("x ==%.f %.f  %.f steps_per_mm:%d,%u"%
                        (self.min_x,self.max_x,x_count,steps_per_mm,
                        stepper.get_oid()))
                    print("kinematics:%s" %
                        self.config.getsection('printer').get('kinematics'))
                    bedmesh = self.printer.lookup_object('bed_mesh', None)
                    print_type=0 # default is 'cartesian'
                    if 'delta' ==(
                      self.config.getsection('printer').get('kinematics')):
                        print_type=2
                    if 'corexy' ==(
                      self.config.getsection('printer').get('kinematics')):
                        print_type=1

                    x=x*1000
                    
                    pr=self.Z_Move_Live_cmd.send([self.oid, 
                        ("3 %u\0" % invert_dir).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("7 %d\0" %
                        (self.min_x-self.x_offset)).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("8 %d\0" % (self.max_x-self.x_offset)).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("9 %d\0" % x_count).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("a %d\0"   % x).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("b %d\0"  % steps_per_mm).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("c %u\0"  % stepper.get_oid()).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("d 0\0" ).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("e %d\0"  % print_type).encode('utf-8')])

                    self.results=[]
                    print("xget:%s " %pr['return_set'])
                if stepper.get_name()=='stepper_y':
                    steps_per_mm = 1.0/stepper.get_step_dist()
                    invert_dir, orig_invert_dir = stepper.get_dir_inverted()
                    y=self.gcode_move.last_position[1]
                    #stepper._query_mcu_position()
                    print("y per_mm:%d,%u"%(steps_per_mm,stepper.get_oid()))
                    #invert_dir, orig_invert_dir = stepper.get_dir_inverted()
                    #bedmesh = self.printer.lookup_object('bed_mesh', None)
                    #bedmesh.bmc.orig_config['mesh_min']
                    y=y*1000

                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("f %d\0"   % y).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("g %d\0"  % steps_per_mm).encode('utf-8')])
                    pr=self.Z_Move_Live_cmd.send([self.oid, 
                        ("h %u\0" % invert_dir).encode('utf-8')])    
                    pr=self.Z_Move_Live_cmd.send([self.oid,
                        ("i %u\0"  % stepper.get_oid()).encode('utf-8')])

                    self.results=[]
                    print("yget:%s " %pr['return_set'])
                    #print(cmd_fmt)
            #self.bd_sensor.I2C_BD_send("1018")#1018// finish reading
        elif  CMD_BD > 100:# gcode M102 Sx live adjust
             self.adjust_range = CMD_BD
             self.bd_sensor.I2C_BD_send("1022")
             step_time=100
             self.toolhead = self.printer.lookup_object('toolhead')
             kin = self.toolhead.get_kinematics()
             for stepper in kin.get_steppers():
                 if stepper.is_active_axis('z'):
                     steps_per_mm = 1.0/stepper.get_step_dist()
                     z=self.gcode_move.last_position[2]
                     stepper._query_mcu_position()
                     invert_dir, orig_invert_dir = stepper.get_dir_inverted()
                     z=z*1000
                     print("z step_at_zero:%d"% z)
                     pr=self.Z_Move_Live_cmd.send([self.oid, ("1 %d\0"
                         % z).encode('utf-8')])
                     pr=self.Z_Move_Live_cmd.send([self.oid, ("2 %u\0"
                         % CMD_BD).encode('utf-8')])
                     pr=self.Z_Move_Live_cmd.send([self.oid, ("3 %u\0"
                         % orig_invert_dir).encode('utf-8')])
                     pr=self.Z_Move_Live_cmd.send([self.oid, ("4 %u\0"
                         % steps_per_mm).encode('utf-8')])
                     pr=self.Z_Move_Live_cmd.send([self.oid, ("5 %u\0"
                         % step_time).encode('utf-8')])
                     pr=self.Z_Move_Live_cmd.send([self.oid, ("6 %u\0"
                         % stepper.get_oid()).encode('utf-8')])
                     print("get:%s " %pr['return_set'])
                     #print(cmd_fmt)
             self.bd_sensor.I2C_BD_send("1018")#1018// finish reading
        self.bd_sensor.I2C_BD_send("1018")#1018// finish reading
        #self.process_m102=0
    def _handle_mcu_identify(self):
        print("BD _handle_mcu_identify")
        kin = self.printer.lookup_object('toolhead').get_kinematics()
        for stepper in kin.get_steppers():
            if stepper.is_active_axis('z'):
                self.add_stepper(stepper)
    def raise_probe(self):
        print("BD raise_probe")
        return
        self.toolhead = self.printer.lookup_object('toolhead')
        start_pos = self.toolhead.get_position()
        self.deactivate_gcode.run_gcode_from_command()
        if self.toolhead.get_position()[:3] != start_pos[:3]:
            raise self.printer.command_error(
                "Toolhead moved during probe activate_gcode script")
    def lower_probe(self):
        print("BD lower_probe0")
        return
        self.toolhead = self.printer.lookup_object('toolhead')
        start_pos = self.toolhead.get_position()
        self.activate_gcode.run_gcode_from_command()
        if self.toolhead.get_position()[:3] != start_pos[:3]:
            raise self.printer.command_error(
                "Toolhead moved during probe deactivate_gcode script")

    def home_start(self, print_time, sample_time, sample_count, rest_time,
                   triggered=True):
        print("BD home_start")
        self.cmd_M102("M102 S-7")
        ENDSTOP_REST_TIME = .001
        rest_time = min(rest_time, ENDSTOP_REST_TIME)
        self.finish_home_complete = self.mcu_endstop.home_start(
            print_time, sample_time, sample_count, rest_time, triggered)
        # Schedule wait_for_trigger callback
        r = self.printer.get_reactor()
        self.wait_trigger_complete = r.register_callback(self.wait_for_trigger)   
        return self.finish_home_complete

    def wait_for_trigger(self, eventtime):
        print("BD wait_for_trigger")  
        self.finish_home_complete.wait()
        if self.multi == 'OFF':
            self.raise_probe()
    def multi_probe_begin(self):
        print("BD multi_probe_begin")
        #self.bd_sensor.I2C_BD_send("1022")
        if self.stow_on_each_sample:
            return
        self.multi = 'FIRST'

    def multi_probe_end(self):
        print("BD multi_probe_end")
        self.bd_sensor.I2C_BD_send("1018")
        if self.stow_on_each_sample:
            return
        self.raise_probe()
        self.multi = 'OFF'
    def probe_prepare(self, hmove):
        print("BD probe_prepare")
        if self.multi == 'OFF' or self.multi == 'FIRST':
            self.lower_probe()
            if self.multi == 'FIRST':
                self.multi = 'ON'
    def probe_finish(self, hmove):
        print("BD probe_finish")
        self.bd_sensor.I2C_BD_send("1018")
        if self.multi == 'OFF':
            self.raise_probe()
    def get_position_endstop(self):
        print("BD get_position_endstop")
        return self.position_endstop

def load_config(config):
    bdl=BDsensorEndstopWrapper(config)
    config.get_printer().add_object('probe', probe.PrinterProbe(config, bdl))
    return bdl
