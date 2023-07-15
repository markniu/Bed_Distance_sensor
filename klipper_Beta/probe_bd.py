 
    def run_probe(self, gcmd):
        speed = gcmd.get_float("PROBE_SPEED", self.speed, above=0.)
        lift_speed = self.get_lift_speed(gcmd)
        sample_count = gcmd.get_int("SAMPLES", self.sample_count, minval=1)
        sample_retract_dist = gcmd.get_float("SAMPLE_RETRACT_DIST",
                                             self.sample_retract_dist, above=0.)
        samples_tolerance = gcmd.get_float("SAMPLES_TOLERANCE",
                                           self.samples_tolerance, minval=0.)
        samples_retries = gcmd.get_int("SAMPLES_TOLERANCE_RETRIES",
                                       self.samples_retries, minval=0)
        samples_result = gcmd.get("SAMPLES_RESULT", self.samples_result)
        must_notify_multi_probe = not self.multi_probe_pending
        if must_notify_multi_probe:
            self.multi_probe_begin()
        probexy = self.printer.lookup_object('toolhead').get_position()[:2]
        retries = 0
        positions = []
        toolhead = self.printer.lookup_object('toolhead')
        #gcmd.respond_info("speed:%.3f"%speed)
        while len(positions) < sample_count:         
            # Probe position
            try:
                if ((self.mcu_probe.bd_sensor is not None) and 
                        (( "BED_MESH_CALIBRATE" in gcmd.get_command()) or
                        ("QUAD_GANTRY_LEVEL" in gcmd.get_command()))):
                    #pos = self._probe(speed)
                    toolhead.wait_moves()
                    time.sleep(0.004)
                    pos = toolhead.get_position()
                    intd=self.mcu_probe.BD_Sensor_Read(0)
                    pos[2]=pos[2]-intd
                    self.gcode.respond_info("probe at %.3f,%.3f is z=%.6f"
                                            % (pos[0], pos[1], pos[2]))
                    #return pos[:3]
                    positions.append(pos[:3])
                    # Check samples tolerance
                    z_positions = [p[2] for p in positions]
                    if max(z_positions) - min(z_positions) > samples_tolerance:
                        if retries >= samples_retries:
                            raise gcmd.error("Probe samples exceed samples_tolerance")
                        gcmd.respond_info("Probe samples exceed tolerance. Retrying...")
                        retries += 1
                        positions = []
                    continue
            except Exception as e:
                #gcmd.respond_info("%s"%str(e))
                #gcmd.respond_info("%s"%str(e))
                #raise gcmd.error("%s"%str(e))
                pass
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
        return self._calc_mean(positions)
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

    def fast_probe_oneline(self, direction):
        
        probe = self.printer.lookup_object('probe', None)
        
        oneline_points = []
        start_point = []
        if "backward" in  direction:
            start_point=list(self.probe_points[len(self.results_copy)-1])
        else:
            start_point=list(self.probe_points[len(self.results)])
        end_point = []
        for point in self.probe_points:
            if start_point[1] is point[1]:
                oneline_points.append(point)
        n_count=len(oneline_points)
        if n_count<=1:
            raise self.printer.config_error(
                "Seems the mesh direction is not X, points count on x is %d" % (n_count))
        if "backward" in  direction:        
            oneline_points.reverse()
        
        end_point = list(oneline_points[n_count-1])
        #print(oneline_points)
        #print(start_point)
        #print(end_point)
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
                if "backward" in  direction:
                    self.results_1.append(pos)
                    self.results_copy.pop()
                else:
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
            self.fast_probe_oneline("forward")
        #print("results:",self.results)    
        self.results_copy = self.results.copy()
        #print(self.results_copy) 
        self.results_1 = []
        while len(self.results_1) < len(self.probe_points):
            self.fast_probe_oneline("backward")
        
        self.results_1.reverse()
        #print("results_1_1:",self.results_1)
        for index in range(len(self.results)):
            self.results[index][2] =  (self.results[index][2] + self.results_1[index][2])/2
            probe.gcode.respond_info("finalize probe at %.3f,%.3f is z=%.6f"
                                        % (self.results[index][0], self.results[index][1], self.results[index][2]))
        res = self.finalize_callback(self.probe_offsets, self.results)
        #print("results:",self.results)
       
        self.results = []
        self.results_1 = []
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
        gcmd.respond_info("g code:%s"%gcmd.get_command())
        if "BED_MESH_CALIBRATE" in gcmd.get_command():
             try:
                 if probe.mcu_probe.no_stop_probe is not None:
                     self.fast_probe(gcmd)
                     probe.multi_probe_end()
                     return
             except AttributeError as e:
                #gcmd.respond_info("%s"%str(e))
                # raise gcmd.error("%s"%str(e))
                pass

        while 1:
            done = self._move_next()
            if done:
                break
            pos = probe.run_probe(gcmd)
            self.results.append(pos)
        probe.multi_probe_end()
#end
