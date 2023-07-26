
// Code for Bed Distance Sensor
//https://github.com/markniu/Bed_Distance_sensor
// Mark yue<niujl123@sina.com>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include<string.h>
#include <stdlib.h>

#include "autoconf.h"
#include "board/gpio.h"
#include "board/irq.h"
#include "board/misc.h"
#include "command.h"
#include "sched.h"


#include "autoconf.h" // CONFIG_*
#include "basecmd.h" // oid_alloc
#include "board/gpio.h" // gpio_out_write
#include "board/irq.h" // irq_disable
#include "board/misc.h" // timer_is_before
#include "command.h" // DECL_COMMAND
//#include "sched.h" // struct timer
//#include "stepper.h" // stepper_event
#include "trsync.h" // trsync_add_signal


#define BYTE_CHECK_OK     0x01
#define BYTE_CHECK_ERR    0x00
#define BD_setLow(x)  gpio_out_write(x,0)
#define BD_setHigh(x) gpio_out_write(x,1)


uint32_t sda_pin=0,scl_pin=0,delay_m=20,homing_pose=0,z_ofset=0;
extern uint16_t BD_Data;
//extern uint32_t timer_period_time;
uint16_t BD_read_flag=1018,BD_read_lock=0;

struct gpio_out sda_gpio, scl_gpio;
struct gpio_in sda_gpio_in;
uint8_t oid_g;
uint8_t z_oid[4];
uint32_t endtime_adjust=0;
uint32_t endtime_debug=0;
uint32_t timer_period_endstop=100;



uint16_t BD_i2c_read(void);
enum { POSITION_BIAS=0x40000000 };

struct stepper {
    struct timer time;
    uint32_t interval;
    int16_t add;
    uint32_t count;
    uint32_t next_step_time, step_pulse_ticks;
    struct gpio_out step_pin, dir_pin;
    uint32_t position;
    struct move_queue_head mq;
    struct trsync_signal stop_signal;
    // gcc (pre v6) does better optimization when uint8_t are bitfields
    uint8_t flags : 8;
};

struct step_adjust{
    uint32_t steps_at_zero;
    int adj_z_range;
    int invert_dir;
    int steps_per_mm;
    int step_time;
    int zoid;//oid for all the z stepper
};

struct step_adjust step_adj[3];//x,y,z

struct _step_probe{
    int min_x;
    int max_x;
    int points;
    int steps_at_zero;
    int steps_per_mm;
    int xoid;//oid for x stepper
    int x_count;
	int x_dir;
	int y_dir;
    int kinematics;//0:cartesian,1:corexy,2:delta
    ////for y
    int min_y;
    int max_y;
    int y_steps_at_zero;
    int y_steps_per_mm;
    int y_oid;//oid for y stepper
    ////
    int x_data[64];
};

struct _step_probe stepx_probe;


void BD_i2c_write(unsigned int addr);



uint16_t Get_Distane_data(void)
{
    BD_read_flag=1018;
    return BD_Data;

}

int BD_i2c_init(uint32_t _sda,uint32_t _scl,uint32_t delays,uint32_t h_pose,uint32_t z_adjust)
{
    sda_pin=_sda;
    scl_pin =_scl;
	homing_pose = h_pose;
	z_ofset = z_adjust;
	if (z_ofset > 500)
		z_ofset = 0;
    if(delays>0)
        delay_m=delays;
    sda_gpio=gpio_out_setup(sda_pin, 1);
    scl_gpio=gpio_out_setup(scl_pin, 1);

    gpio_out_write(sda_gpio, 1);
    gpio_out_write(scl_gpio, 1);
    step_adj[0].zoid=0;

    stepx_probe.xoid=0;
    stepx_probe.y_oid=0;
	endtime_adjust=0;
	//output("BD_i2c_init mcuoid=%c sda:%c scl:%c dy:%c h_p:%c", oid_g,sda_pin,scl_pin,delay_m,homing_pose);
	BD_i2c_write(1022); //reset BDsensor
    return 1;
}

uint32_t nsecs_to_ticks_bd(uint32_t ns)
{
    //return timer_from_us(ns * 1000) / 1000000;
	return ns * (CONFIG_CLOCK_FREQ / 1000000);
}


void ndelay_bd_c(uint32_t nsecs)
{
    if (CONFIG_MACH_AVR)
        return;
    uint32_t end = timer_read_time() + nsecs_to_ticks_bd(nsecs);
    while (timer_is_before(timer_read_time(), end))
        irq_poll();
}
void ndelay_bd(uint32_t nsecs)
{
    int i=1;
	while(i--)
		ndelay_bd_c(nsecs);
}

unsigned short BD_Add_OddEven(unsigned short byte)
{
    unsigned char i;
    unsigned char n;
    unsigned short r;
    n =0;
    for(i=0;i<10;i++)
    {
        if(((byte >>i)&0x01) == 0x01)
        {
            n++;
        }
    }
    if((n&0x01) == 0x01)
    {
        r = byte | 0x400;
    }
    else
    {
        r = byte | 0x00;
    }
    return r;
}

unsigned short BD_Check_OddEven(unsigned short byte)
{
    unsigned char i;
    unsigned char n;
    unsigned char r;
    n =0;
    for(i=0;i<10;i++)
    {
        if(((byte >>i)&0x01) == 0x01)
        {
           n++;
        }
    }
    if((byte>>10) == (n&0x01))
    {
        r = BYTE_CHECK_OK;
    }
    else
    {
        r = BYTE_CHECK_ERR;
    }
    return r;
}

void BD_I2C_start(void)
{
    sda_gpio=gpio_out_setup(sda_pin, 1);
    scl_gpio=gpio_out_setup(scl_pin, 1);
    BD_setHigh(scl_gpio);
    BD_setHigh(sda_gpio);
    ndelay_bd(delay_m);
    BD_setLow(sda_gpio);
    ndelay_bd(delay_m);
    BD_setLow(scl_gpio);
    ndelay_bd(delay_m);
}
void  BD_i2c_stop(void)
{
    ndelay_bd(delay_m);
    sda_gpio=gpio_out_setup(sda_pin, 1);
    BD_setLow(sda_gpio);
    ndelay_bd(delay_m);
    scl_gpio=gpio_out_setup(scl_pin, 1);
    BD_setHigh(scl_gpio);
    ndelay_bd(delay_m);

    BD_setHigh(sda_gpio);
    ndelay_bd(delay_m);
}

uint16_t BD_i2c_read(void)
{
    uint16_t b = 1024;
   // if(BD_read_flag==1014)

    BD_read_lock=1;
    BD_I2C_start();

    BD_setHigh(sda_gpio);
    BD_setHigh(scl_gpio);
    ndelay_bd(delay_m);
    BD_setLow(scl_gpio);

    ndelay_bd(delay_m);
    b = 0;
    BD_setHigh(sda_gpio);
    sda_gpio_in=gpio_in_setup(sda_pin, 1);
    for (unsigned char i = 0; i <= 10; i++) {
        b <<= 1;
        ndelay_bd(delay_m);
        BD_setHigh(scl_gpio);
        if (gpio_in_read(sda_gpio_in))
            b |= 1;
        ndelay_bd(delay_m);
        BD_setLow(scl_gpio);
    }
    BD_i2c_stop();
    if (BD_Check_OddEven(b) && (b & 0x3FF) < 1020){
        b = (b & 0x3FF);
		b = b + z_ofset;
    }
	else
		b=1024;
    if(b>1024)
        b=1024;
#if 0
    sda_gpio_in=gpio_in_setup(sda_pin, 1);
	b=0;
    b=gpio_in_read(sda_gpio_in);
#endif
#if 0

    if((endtime_debug%8000)==0)
    {
		output("BDread mcuoid=%c b:%c sda:%c scl:%c dy:%c", oid_g,b,sda_pin,scl_pin,delay_m);
    }
	endtime_debug++;
#endif	
    BD_read_lock=0;
    return b;
}

void BD_i2c_write(unsigned int addr)
{
    BD_I2C_start();
    //// write
    BD_setLow(sda_gpio);
    BD_setHigh(scl_gpio);
    ndelay_bd(delay_m*2);
    BD_setLow(scl_gpio);
    addr=BD_Add_OddEven(addr);
    ///write address
    ndelay_bd(delay_m);
    for (int i=10; i >=0; i--)
    {
        if ((addr>>i)&0x01)
        {
            BD_setHigh(sda_gpio);
        }
        else
            BD_setLow(sda_gpio);
        ndelay_bd(delay_m);
        BD_setHigh(scl_gpio);
        ndelay_bd(delay_m*2);
        BD_setLow(scl_gpio);
        ndelay_bd(delay_m);
    }
    BD_i2c_stop();
}

uint32_t INT_to_String(uint32_t BD_z1,uint8_t*data)
{
    uint32_t BD_z=BD_z1;
    uint32_t len=0,j=0;
    if(BD_z>=1000)
    {
        j=BD_z/1000;
        data[len++] = '0'+j;
        BD_z-=1000*j;
        data[len]='0';
        data[len+1]='0';
        data[len+2]='0';
    }
    if(BD_z>=100)
    {
        j=BD_z/100;
        data[len++] = '0'+j;
        BD_z-=100*j;
        data[len]='0';
        data[len+1]='0';
    }
    else if(data[len])
        len++;
    if(BD_z>=10)
    {
        j=BD_z/10;
        data[len++] = '0'+j;
        BD_z-=10*j;
        data[len]='0';
    }
    else if(data[len])
        len++;
    j=BD_z;
    data[len++] = '0'+j;
    data[len]=0;
    return len;
}


extern   uint32_t
stepper_get_position(struct stepper *s);
extern   struct stepper *
stepper_oid_lookup(uint8_t oid);

//for gcode command
void
command_I2C_BD_receive(uint32_t *args)
{
    uint8_t oid = args[0];
    uint8_t data[8];
    uint16_t BD_z;

    //if(BD_read_flag==1018)
    //    BD_z=BD_Data;
    //else
    BD_z=BD_i2c_read();//BD_Data;
    BD_Data=BD_z;
    memset(data,0,8);
    uint32_t len=0,j=0;

///////////same as function itoa()
    if(BD_z>=1000)
    {
        j=BD_z/1000;
        data[len++] = '0'+j;
        BD_z-=1000*j;
        data[len]='0';
        data[len+1]='0';
        data[len+2]='0';
    }
    if(BD_z>=100)
    {
        j=BD_z/100;
        data[len++] = '0'+j;
        BD_z-=100*j;
        data[len]='0';
        data[len+1]='0';
    }
    else if(data[len])
        len++;
    if(BD_z>=10)
    {
        j=BD_z/10;
        data[len++] = '0'+j;
        BD_z-=10*j;
        data[len]='0';
    }
    else if(data[len])
        len++;
    j=BD_z;
    data[len++] = '0'+j;

    sendf("I2C_BD_receive_response oid=%c response=%*s", oid,len,data);
}

DECL_COMMAND(command_I2C_BD_receive, "I2C_BD_receive oid=%c data=%*s");


void
command_I2C_BD_send(uint32_t *args)
{
    int addr=atoi((char *)args[2]);
    BD_read_flag=addr;
    if(addr==1015)
        return;
    BD_i2c_write(addr);

}

DECL_COMMAND(command_I2C_BD_send, "I2C_BD_send oid=%c data=%*s");


void
command_Z_Move_Live(uint32_t *args)
{
    //uint32_t position = stepper_get_position(s)- POSITION_BIAS;
    int i=0,j=0;
    //int adj_z_range;
    //char data[30];
    char *tmp;
    uint8_t oid = args[0];
    tmp=(char *)args[2];
    j=atoi(tmp+2);
    if(tmp[0]=='1')
        step_adj[0].steps_at_zero=j;
    else if(tmp[0]=='2')
        step_adj[0].adj_z_range=j;
    else if(tmp[0]=='3')
    {
        step_adj[0].invert_dir=j;
		if(j==1)
			stepx_probe.x_dir=1;
		else
			stepx_probe.x_dir=-1;
    }
    else if(tmp[0]=='4')
    {
        step_adj[0].steps_per_mm=j;
    }
    else if(tmp[0]=='5')
        step_adj[0].step_time=j;
    else if(tmp[0]=='6')
    {
     /*   struct stepper *s = stepper_oid_lookup(j);
        uint32_t cur_stp=stepper_get_position(s);
        step_adj[0].zoid=j;
        step_adj[0].steps_at_zero=
            cur_stp-(step_adj[0].steps_at_zero*step_adj[0].steps_per_mm)/1000;
            */
//        output("Z_Move_L mcuoid=%c zero=%c", oid,step_adj[0].steps_at_zero);
    }
//for debug  report postion when the motor
    else if(tmp[0]=='7')
    {
        stepx_probe.min_x=j;
    }
    else if(tmp[0]=='8')
    {
        stepx_probe.max_x=j;
    }

    else if(tmp[0]=='9')
    {
        stepx_probe.points=j;
    }
    else if(tmp[0]=='a')
    {
        stepx_probe.steps_at_zero=j;
    }
    else if(tmp[0]=='b')
    {
        stepx_probe.steps_per_mm=j;
    }
    else if(tmp[0]=='c')
    {/*
        stepx_probe.xoid=j;
		if(stepx_probe.xoid){
	        struct stepper *s = stepper_oid_lookup(j);
	        uint32_t cur_stp=stepper_get_position(s);
	        stepx_probe.steps_at_zero=cur_stp+
	            stepx_probe.x_dir*(stepx_probe.steps_at_zero*stepx_probe.steps_per_mm)/1000;
	        stepx_probe.min_x=stepx_probe.min_x*stepx_probe.steps_per_mm
	            +stepx_probe.steps_per_mm;
	        stepx_probe.max_x=stepx_probe.max_x*stepx_probe.steps_per_mm
	            -stepx_probe.steps_per_mm;
	       // output("Z_Move_L mcuoid=%c zero=%c", oid,stepx_probe.max_x);
	        stepx_probe.x_count=0;
		}
		*/
    }
    else if(tmp[0]=='d')
    {
        stepx_probe.x_count=j;
    }
    else if(tmp[0]=='e')
    {
        stepx_probe.kinematics=j;
    }
    ///////////////////// motor y
    else if(tmp[0]=='f')
    {
        stepx_probe.y_steps_at_zero=j;
    }
    else if(tmp[0]=='g')
    {
        stepx_probe.y_steps_per_mm=j;
    }
	else if(tmp[0]=='h')
    {
		if(j==1)
			stepx_probe.y_dir=1;
		else
			stepx_probe.y_dir=-1;
    }
    else if(tmp[0]=='i')
    {
    /*
        stepx_probe.y_oid=j;
		if(stepx_probe.y_oid){
	        struct stepper *s = stepper_oid_lookup(j);
	        uint32_t cur_stp=stepper_get_position(s);
	        stepx_probe.y_steps_at_zero=cur_stp+
	            stepx_probe.y_dir*(stepx_probe.y_steps_at_zero*stepx_probe.y_steps_per_mm)/1000;
		}
		*/
    }
	else if(tmp[0]=='j')
	{
		//timer_period_time=j;
	}
	else if(tmp[0]=='k')
	{
		timer_period_endstop=j;
		endtime_adjust=0;
	}
	else if(tmp[0]=='m')
	{
		homing_pose=j; //0.01mm
	}

    //output("Z_Move_L mcuoid=%c j=%c %c %c", oid,j,stepx_probe.xoid,stepx_probe.y_oid);

    sendf("Z_Move_Live_response oid=%c return_set=%*s", oid,i,(char *)args[2]);
}
DECL_COMMAND(command_Z_Move_Live, "Z_Move_Live oid=%c data=%*s");

//"Z_Move_Live oid=%c z_oid=%c dir=%u step=%u delay=%u"



void
command_config_I2C_BD(uint32_t *args)
{
    oid_g = args[0];
    BD_i2c_init(args[1],args[2],args[3],args[4],args[5]);
}
DECL_COMMAND(command_config_I2C_BD,
             "config_I2C_BD oid=%c sda_pin=%u scl_pin=%u delay=%u h_pos=%u z_adjust=%u");


 void
 bd_sensor_task(void)
 {

    //uint32_t len=0;
    uint16_t tm;


    if(BD_read_flag!=1018)
        return;

    if(sda_pin==0||scl_pin==0)
        return;
    if(timer_period_endstop>=100)
		return;
    if(endtime_adjust>(timer_read_time() + timer_from_us(timer_period_endstop*1000*2)))
		endtime_adjust=timer_read_time() + timer_from_us(timer_period_endstop*1000);//us
		
	if(endtime_adjust>timer_read_time())
	    return;
	endtime_adjust=timer_read_time() + timer_from_us(timer_period_endstop*1000);//us
    
    tm=BD_i2c_read();
    if(tm<1023)
    {
        BD_Data=tm;
    }
	else
		BD_Data=0;

    if(BD_Data<=homing_pose)
		BD_Data=0;
   // len=INT_to_String(BD_Data,data);
   // sendf("BD_Update oid=%c distance_val=%*s", oid_g,len,data);

 }
 DECL_TASK(bd_sensor_task);
