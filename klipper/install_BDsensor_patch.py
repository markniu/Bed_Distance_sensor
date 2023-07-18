import os
import sys

home_dir=os.environ['HOME']
BD_type="klipper"
try:
   # print (sys.argv[0])
    home_dir=sys.argv[1]         
except Exception as e:
    #print("%s"%str(e))
    pass
if sys.argv[0].find("klipper_Beta") > 0:
    BD_type="klipper_Beta"
    print (BD_type)  

print("the path of klipper is '"+home_dir+"' ? \nusage example:python /home/pi/Bed_Distance_sensor/klipper/install_BDsensor_patch.py "+home_dir+"\n")
BD_dir=home_dir+"/Bed_Distance_sensor/"+BD_type
print(BD_dir)


##copy BDsensor.py
with open(BD_dir+'/BDsensor.py', 'r') as file:
    data = file.read().rstrip() 
    with open(home_dir+'/klipper/klippy/extras/BDsensor.py', "w") as text_file:
        text_file.write("%s" % data)
##copy BD_sensor.c
with open(BD_dir+'/BD_sensor.c', 'r') as file:
    data = file.read().rstrip() 
    with open(home_dir+'/klipper/src/BD_sensor.c', "w") as text_file:
        text_file.write("%s" % data)



#####

##replace run_probe
with open(BD_dir+'/probe_bd.py', 'r') as file:
    Bdata = file.read().rstrip()
    
    Bstart=Bdata.find("def run_probe(self, gcmd):")
    Bend=Bdata.find(" def ",Bstart)
    if Bend<0 :
        Bend=len(Bdata)

#print("BD%d,%d,%d"%(Bstart,Bend,len(Bdata)))

with open(home_dir+'/klipper/klippy/extras/probe.py', 'r') as file:
    data = file.read().rstrip()    
    start=data.find("def run_probe(self, gcmd):")
    end=data.find(" cmd_PROBE_help = ",start)
    with open(home_dir+'/klipper/klippy/extras/probe.py', "w") as text_file:
        text_file.write("%s%s\n   %s" % (data[0:start],Bdata[Bstart:Bend],data[end:len(data)]))

#with open("BDsensor.py", "w") as text_file:
#    text_file.write("%s %s" % (Bdata[0:Bstart],Bdata[Bend:len(Bdata)]))

##replace start_probe

with open(BD_dir+'/probe_bd.py', 'r') as file:
    Bdata = file.read().rstrip()
    
    Bstart=Bdata.find("def _move_next(self):")
    Bend=Bdata.find("\n#end",Bstart)
    if Bend<0 :
        Bend=len(Bdata)

#print("BD%d,%d,%d"%(Bstart,Bend,len(Bdata)))

with open(home_dir+'/klipper/klippy/extras/probe.py', 'r') as file:
    data = file.read().rstrip()    
    start=data.find("def _move_next(self):")
    end=data.find("    def _manual_probe_start(self):",start)
    with open(home_dir+'/klipper/klippy/extras/probe.py', "w") as text_file:
        text_file.write("%s%s\n%s" % (data[0:start],Bdata[Bstart:Bend],data[end:len(data)]))

#with open("BDsensor.py", "w") as text_file:
#    text_file.write("%s %s" % (Bdata[0:Bstart],Bdata[Bend:len(Bdata)]))



##replace endstop.c    
with open(BD_dir+'/endstop_bd.c', 'r') as file:
    Bdata = file.read().rstrip()
    
    Bstart=Bdata.find("////replace endstop.c")
    Bend=Bdata.find("////end replace",Bstart)
    if Bend<0 :
        Bend=len(Bdata)


with open(home_dir+'/klipper/src/endstop.c', 'r') as file:
    data = file.read().rstrip()
    data=data.replace("gpio_in_read(e->pin)","read_endstop_pin(e)")
    data=data.replace("gpio_in_setup(args[1], args[2]);","gpio_in_setup(args[1], args[2]);\n    e->type = args[2];")
    start=data.find("struct endstop {\n    struct timer time;")
    end=data.find("enum {",start)
    if start > 0 :
        with open(home_dir+'/klipper/src/endstop.c', "w") as text_file:
            text_file.write("%s%s\n   %s" % (data[0:start],Bdata[Bstart:Bend],data[end:len(data)]))



##replace src/Makefile
with open(home_dir+'/klipper/src/Makefile', 'r') as file:
    data = file.read().rstrip()
    data=data.replace("basecmd.c debugcmds.c","basecmd.c BD_sensor.c debugcmds.c")
    with open(home_dir+'/klipper/src/Makefile', "w") as text_file:
        text_file.write("%s" % (data))
           
##        
with open(home_dir+'/klipper/klippy/extras/probe.py', 'r') as file:
    data = file.read().rstrip()
    #data=data.replace("sample_retract_dist, above=0.)","sample_retract_dist, )")
    data=data.replace("import logging\nimport pins","import logging,time,copy\nimport pins")
    with open(home_dir+'/klipper/klippy/extras/probe.py', "w") as text_file:
        text_file.write("%s" % (data))

print("Install Bed Distance Sensor into Klipper successfully\n")
