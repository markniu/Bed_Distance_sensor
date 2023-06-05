    def _probe(self, speed):
        toolhead = self.printer.lookup_object('toolhead')
        curtime = self.printer.get_reactor().monotonic()
        if 'z' not in toolhead.get_status(curtime)['homed_axes']:
            raise self.printer.command_error("Must home before probe")
        phoming = self.printer.lookup_object('homing')
        pos = toolhead.get_position()
        pos[2] = self.z_position
        #For BD sensor
        try:
            #self.mcu_probe.bd_sensor.I2C_BD_send("1022")
            if self.mcu_probe.bd_sensor is not None:
                toolhead.wait_moves()
                pos = toolhead.get_position()
                intd=self.mcu_probe.BD_Sensor_Read(0)
                pos[2]=pos[2]-intd
                self.gcode.respond_info("probe at %.3f,%.3f is z=%.6f"
                                        % (pos[0], pos[1], pos[2]))
                return pos[:3]
        except Exception as e:
            pass
        try:
            epos = phoming.probing_move(self.mcu_probe, pos, speed)
        except self.printer.command_error as e:
            reason = str(e)
            if "Timeout during endstop homing" in reason:
                reason += HINT_TIMEOUT
            raise self.printer.command_error(reason)
        self.gcode.respond_info("probe at %.3f,%.3f is z=%.6f"
                                % (epos[0], epos[1], epos[2]))
        return epos[:3]

    def _move_next(self):
        toolhead = self.printer.lookup_object('toolhead')
        # Lift toolhead
        speed = self.lift_speed
        if not self.results:
            # Use full speed to first probe position
            speed = self.speed
        toolhead.manual_move([None, None, self.horizontal_move_z], speed)
        # Check if done probing
        if len(self.results) >= len(self.probe_points):
            toolhead.get_last_move_time() 
            res = self.finalize_callback(self.probe_offsets, self.results)
            if res != "retry":
                return True
            self.results = []
        # Move to next XY probe point
        nextpos = list(self.probe_points[len(self.results)])
        if self.use_offsets:
            nextpos[0] -= self.probe_offsets[0]
            nextpos[1] -= self.probe_offsets[1]
        toolhead.manual_move(nextpos, self.speed)
        return False

    def fast_probe_oneline(self, gcmd):
        
        probe = self.printer.lookup_object('probe', None)
        
        oneline_points = []
        start_point=list(self.probe_points[len(self.results)])
        end_point = []
        for point in self.probe_points:
            if start_point[1] is point[1]:
                oneline_points.append(point)
        n_count=len(oneline_points)
        if n_count<=1:
            raise self.printer.config_error(
                "Seems the mesh direction is not X, points count on x is %d" % (n_count))
        end_point = list(oneline_points[n_count-1])  
        print(oneline_points)
        print(start_point)
        print(end_point)
        toolhead = self.printer.lookup_object('toolhead')
        if self.use_offsets:
            start_point[0] -= self.probe_offsets[0]
            start_point[1] -= self.probe_offsets[1]
            end_point[0] -= self.probe_offsets[0]
            end_point[1] -= self.probe_offsets[1]
        toolhead.manual_move(start_point, self.speed)
        toolhead.wait_moves()
        toolhead.manual_move(end_point, self.speed)
        ####
        toolhead._flush_lookahead()
        curtime = toolhead.reactor.monotonic()
        est_time =toolhead.mcu.estimated_print_time(curtime)
        line_time = toolhead.print_time-est_time
        start_time = est_time
        x_index = 0
        
        while (not toolhead.special_queuing_state
               or toolhead.print_time >= est_time):
            if not toolhead.can_pause:
                break                
            est_time =toolhead.mcu.estimated_print_time(curtime)    
            
            if (est_time-start_time) >= x_index*line_time/(n_count-1):    
                print(" est:%f,t:%f,dst:%f"%(est_time,(est_time-start_time),x_index*line_time/n_count))
                pos = toolhead.get_position()
                pos[0] = oneline_points[x_index][0]
                pos[1] = oneline_points[x_index][1]
                #pr = probe.mcu_probe.I2C_BD_receive_cmd.send([probe.mcu_probe.oid, "32".encode('utf-8')])
                #intd=int(pr['response'])
                intd=probe.mcu_probe.BD_Sensor_Read(0)
                pos[2]=pos[2]-intd
                probe.gcode.respond_info("probe at %.3f,%.3f is z=%.6f"
                                        % (pos[0], pos[1], pos[2]))
               # return pos[:3]
               # pos = probe.run_probe(gcmd)
                self.results.append(pos)
                x_index += 1;
            curtime = toolhead.reactor.pause(curtime + 0.001)
            
    def fast_probe(self, gcmd):
        toolhead = self.printer.lookup_object('toolhead')
        probe = self.printer.lookup_object('probe', None)
        speed = self.lift_speed
        if not self.results:
            # Use full speed to first probe position
            speed = self.speed
        toolhead.manual_move([None, None, self.horizontal_move_z], speed)
        self.results = []
        while len(self.results) < len(self.probe_points):
            self.fast_probe_oneline(gcmd)
        res = self.finalize_callback(self.probe_offsets, self.results)
        print(self.results)        
        self.results = []
        if res != "retry":
            return True

    def start_probe(self, gcmd):
        manual_probe.verify_no_manual_probe(self.printer)
        # Lookup objects
        probe = self.printer.lookup_object('probe', None)
        method = gcmd.get('METHOD', 'automatic').lower()
        self.results = []
        def_move_z = self.default_horizontal_move_z
        self.horizontal_move_z = gcmd.get_float('HORIZONTAL_MOVE_Z',
                                                def_move_z)
        if probe is None or method != 'automatic':
            # Manual probe
            self.lift_speed = self.speed
            self.probe_offsets = (0., 0., 0.)
            self._manual_probe_start()
            return
        # Perform automatic probing
        self.lift_speed = probe.get_lift_speed(gcmd)
        self.probe_offsets = probe.get_offsets()
        if self.horizontal_move_z < self.probe_offsets[2]:
            raise gcmd.error("horizontal_move_z can't be less than"
                             " probe's z_offset")
        probe.multi_probe_begin()
        
        if gcmd.get_command() == "BED_MESH_CALIBRATE":
             try:
                 if probe.mcu_probe.no_stop_probe is not None:
                     self.fast_probe(gcmd)
                     probe.multi_probe_end() 
                     return
             except AttributeError as e:
                 pass
        while 1:
            done = self._move_next()
            if done:
                break
            pos = probe.run_probe(gcmd)
            self.results.append(pos)
        probe.multi_probe_end()  
#end
