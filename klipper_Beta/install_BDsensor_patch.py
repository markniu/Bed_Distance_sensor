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
if sys.argv[0].find("klipper_Beta") >= 0:
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

##replace src/Makefile
with open(home_dir+'/klipper/src/Makefile', 'r') as file:
    data = file.read().rstrip()
    data=data.replace("basecmd.c debugcmds.c","basecmd.c BD_sensor.c debugcmds.c")
    with open(home_dir+'/klipper/src/Makefile', "w") as text_file:
        text_file.write("%s" % (data))
##        
with open(home_dir+'/klipper/.git/info/exclude', 'r') as file:
    data = file.read().rstrip()
    if "BDsensor.py" not in data:
        data+="\nklippy/extras/BDsensor.py\nsrc/BD_sensor.c\n"
    
    with open(home_dir+'/klipper/.git/info/exclude', "w") as text_file:
        text_file.write("%s" % (data))

print("Install Bed Distance Sensor into Klipper successfully\n")
