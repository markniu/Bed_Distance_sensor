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
            self.mcu_probe.bd_sensor.I2C_BD_send("1022")
            toolhead.wait_moves()
            pos = toolhead.get_position()
            print(pos[2])
            pr = self.mcu_probe.I2C_BD_receive_cmd.send([self.mcu_probe.oid, "32".encode('utf-8')])
            intd=int(pr['response'])
            strd=str(intd/100.0)
            pos[2]=pos[2]-(intd/100.0)
            self.gcode.respond_info("probe at %.3f,%.3f is z=%.6f"
                                    % (pos[0], pos[1], pos[2]))
            return pos[:3]
        except AttributeError as e:
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
        try:
            if probe.mcu_probe.no_stop_probe is not None:
                self._move_next()
                toolhead = self.printer.lookup_object('toolhead')
                toolhead.wait_moves()
                pos = toolhead.get_position()
                print(pos[2])
                probe.mcu_probe.results=[]
                probe.mcu_probe.Z_Move_Live_cmd.send(
                    [probe.mcu_probe.oid, ("d 0\0").encode('utf-8')])
        except AttributeError as e:
            pass
        while 1:
            done = self._move_next()
            if done:
                break
            try:
                if probe.mcu_probe.no_stop_probe is not None:
                    continue
            except AttributeError as e:
                pass
            pos = probe.run_probe(gcmd)
            self.results.append(pos)
        probe.multi_probe_end()   

    def _move_next(self):
        toolhead = self.printer.lookup_object('toolhead')
        probe = self.printer.lookup_object('probe', None)
        # Lift toolhead
        speed = self.lift_speed
        if not self.results:
            # Use full speed to first probe position
            speed = self.speed
        toolhead.manual_move([None, None, self.horizontal_move_z], speed)
        # Check if done probing
        if len(self.results) >= len(self.probe_points):
            toolhead.get_last_move_time()
            try:
                if probe.mcu_probe.no_stop_probe is not None:
                    toolhead = self.printer.lookup_object('toolhead')
                    toolhead.wait_moves()
                    probe = self.printer.lookup_object('probe', None)
                    print(probe.mcu_probe.results)
                    for i in range(len(self.results)):
                        self.results[i][2]=self.horizontal_move_z-\
                          (probe.mcu_probe.results[i]/100.0)
                        self.gcode.respond_info("probe at %.3f,%.3f is z=%.6f"
                                    % (self.results[i][0],
                                    self.results[i][1],
                                    self.results[i][2]))
                    print(self.results)
            except AttributeError as e:
                pass
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
        try:
            if probe.mcu_probe.no_stop_probe is not None:
                p_results=[0.0,0.0,0.0]
                p_results[0]=nextpos[0]
                p_results[1]=nextpos[1]
                p_results[2]=0.02
                self.results.append(p_results)
        except AttributeError as e:
            pass
        return False
