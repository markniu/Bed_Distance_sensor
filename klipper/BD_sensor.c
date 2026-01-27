// Code for Bed Distance Sensor
// Mark yue<niujl123@sina.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h>
#include <stdlib.h>
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
#include "sched.h" // struct timer
#include "stepper.h" // stepper_event
#include "trsync.h" // trsync_add_signal

#define CMD_READ_DATA                 1015
#define CMD_READ_VERSION              1016
#define CMD_START_READ_CALIBRATE_DATA 1017
#define CMD_DISTANCE_MODE             1018
#define CMD_START_CALIBRATE           1019
#define CMD_END_CALIBRATE             1021
#define CMD_REBOOT_SENSOR             1022
#define CMD_SWITCH_MODE               1023
#define DATA_ERROR                    1024

#define CMD_Z_INDEX                    1025
#define CMD_CUR_Z                      1026
#define CMD_ADJ_Z                      1027
#define CMD_DIR_INV                    1028
#define CMD_STEP_MM                    1029
#define CMD_ZOID                       1030
#define CMD_RT_SAMPLE_TIME             1031
#define CMD_RT_RANGE                   1032


#define BYTE_CHECK_OK     0x01
#define BYTE_CHECK_ERR    0x00

#define BD_setLow(x)  gpio_out_write(x,0)
#define BD_setHigh(x) gpio_out_write(x,1)


uint8_t oid_g;
struct endstop {
    struct timer time;
    uint32_t rest_time, sample_time, nextwake,pin_num;
    struct trsync *ts;
    uint8_t flags, sample_count, trigger_count, trigger_reason;
    struct gpio_in pin;
};

enum { ESF_PIN_HIGH=1<<0, ESF_HOMING=1<<1 };

uint32_t delay_m = 20, homing_pose = 0;
int sda_pin = -1, scl_pin = -1, z_ofset = 0;
uint16_t BD_Data;
uint16_t BD_read_flag=CMD_DISTANCE_MODE;
int switch_mode = 0; //1:in switch mode
struct gpio_out sda_gpio, scl_gpio;
struct gpio_in sda_gpio_in;
static uint_fast8_t endstop_oversample_event(struct timer *t);
static struct endstop e ;

int z_index=0;
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
    uint32_t cur_z;
    int adj_z_range;
    int invert_dir;
    int steps_per_mm;
    int step_time;
    int zoid;//oid for all the z stepper
};

#define NUM_Z_MOTOR  6
struct step_adjust step_adj[NUM_Z_MOTOR];//x,y,z
enum {
    SF_LAST_DIR=1<<0, SF_NEXT_DIR=1<<1, SF_INVERT_STEP=1<<2, SF_NEED_RESET=1<<3,
    SF_SINGLE_SCHED=1<<4, SF_HAVE_ADD=1<<5
};
struct endstop bd_tim ;

int32_t	diff_step=0,RT_SAMPLE_TIME=0,adjusted_step=0,RT_RANGE=300;//1000==1mm

int abs_bd(int a, int b){
 if (a > b)
    return a-b;
 else
 	return b-a;
}
extern void
command_config_stepper(uint32_t *args);
void adust_Z_live(uint16_t sensor_z);
void BD_i2c_write(unsigned int addr);
uint16_t BD_i2c_read(void);
void adust_Z_calc(uint16_t sensor_z,struct stepper *s);

void timer_bd_init(void);
int  read_endstop_pin(void);


int BD_i2c_init(uint32_t _sda,uint32_t _scl,
    uint32_t delays,uint32_t h_pose,int z_adjust,uint32_t enstop_num)
{
    int i=0;
	e.pin_num = enstop_num;
    e.pin =  gpio_in_setup(e.pin_num, 1);
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
	for (i=0;i<NUM_Z_MOTOR;i++){
        step_adj[i].cur_z=0;
    	step_adj[i].zoid=0;
		step_adj[i].adj_z_range=0;
	}
    BD_i2c_write(CMD_REBOOT_SENSOR); //reset BDsensor
	////
 	if(CONFIG_CLOCK_FREQ>100000000)
		RT_SAMPLE_TIME= timer_from_us(11000);
	else if(CONFIG_CLOCK_FREQ>60000000)
		RT_SAMPLE_TIME= timer_from_us(16000);
	else //if(CONFIG_CLOCK_FREQ>60000000)
		RT_SAMPLE_TIME= timer_from_us(19000);
    return 1;
}

uint32_t nsecs_to_ticks_bd(uint32_t ns)
{
    return ns * (CONFIG_CLOCK_FREQ / 1000000);
}

void ndelay_bd_c(uint32_t nsecs)
{
   // if (CONFIG_MACH_AVR)
   //     return;
    uint32_t end = timer_read_time() + nsecs_to_ticks_bd(nsecs);
    while (timer_is_before(timer_read_time(), end));
       // irq_poll();
}

void ndelay_bd(uint32_t nsecs)
{
    int i=1;
    while(i--)
        ndelay_bd_c(nsecs);
}

unsigned short BD_Add_OddEven(unsigned short send_data)
{
    unsigned char i;
    unsigned char n;
    unsigned short out_data;
    n =0;
    for(i=0;i<10;i++){
        if(((send_data >>i)&0x01) == 0x01){
            n++;
        }
    }
    if((n&0x01) == 0x01){
        out_data = send_data | 0x400;
    }
    else{
        out_data = send_data | 0x00;
    }
    return out_data;
}

unsigned short BD_Check_OddEven(unsigned short rec_data)
{
    unsigned char i;
    unsigned char n;
    unsigned char ret;
    n =0;
    for(i=0;i<10;i++){
        if(((rec_data >>i)&0x01) == 0x01){
           n++;
        }
    }
    if((rec_data>>10) == (n&0x01)){
        ret = BYTE_CHECK_OK;
    }
    else{
        ret = BYTE_CHECK_ERR;
    }
    return ret;
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
    uint16_t b = DATA_ERROR;
    BD_I2C_start();
    BD_setHigh(sda_gpio);
    BD_setHigh(scl_gpio);
    ndelay_bd(delay_m);
    BD_setLow(scl_gpio);

    ndelay_bd(delay_m);
    b = 0;
    BD_setHigh(sda_gpio);
    sda_gpio_in=gpio_in_setup(sda_pin,0);
    for (unsigned char i = 0; i <= 10; i++) {
        b <<= 1;
        ndelay_bd(delay_m);
        BD_setHigh(scl_gpio);
		ndelay_bd(delay_m);
       // if (gpio_in_read(sda_gpio_in))
        b |= gpio_in_read(sda_gpio_in);
      //  ndelay_bd(delay_m);
        BD_setLow(scl_gpio);
    }
    BD_i2c_stop();
    if (BD_Check_OddEven(b) && (b & 0x3FF) < 1020){
        b = (b & 0x3FF);
        if(BD_read_flag==CMD_DISTANCE_MODE&&(b<1000)){
            b = b - z_ofset;
            if(b>DATA_ERROR)
                b=0;
        }

    }
    else
        b=DATA_ERROR;
 #if 0
    sda_gpio_in=gpio_in_setup(sda_pin,0);
    b=gpio_in_read(sda_gpio_in)*100;
 #endif
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
        if ((addr>>i)&0x01){
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

struct stepper *
stepper_oid_lookup_bd(uint8_t oid)
{
    return oid_lookup(oid, command_config_stepper);
}


void adjust_z_move(void)
{
	struct stepper *s = stepper_oid_lookup_bd(step_adj[0].zoid);
	int dir=0,dir_t=0;//down
	if(s->count){
		//diff_step = 0;
		return;
	}

	if(diff_step>0){
		diff_step--;
		adjusted_step--;
	}
	else{
		diff_step++;
        adjusted_step++;
	}

	for(int i=0;i<NUM_Z_MOTOR;i++){
		if(step_adj[i].zoid==0)
			continue;
		s = stepper_oid_lookup_bd(step_adj[i].zoid);
		dir=diff_step>0?0:1;
		if(step_adj[i].invert_dir==1)
			dir=dir?0:1;
        dir_t=!!(s->flags&SF_LAST_DIR);
		if(dir_t != dir){
			gpio_out_toggle_noirq(s->dir_pin);
		}

	}
	for(int i=0;i<NUM_Z_MOTOR;i++){
		if(step_adj[i].zoid==0)
			continue;
		s = stepper_oid_lookup_bd(step_adj[i].zoid);
		gpio_out_toggle_noirq(s->step_pin);

	}

	for(int i=0;i<NUM_Z_MOTOR;i++){
		if(step_adj[i].zoid==0)
			continue;
		s = stepper_oid_lookup_bd(step_adj[i].zoid);
		dir=diff_step>0?0:1;
		if(step_adj[i].invert_dir==1)
			dir=dir?0:1;
        dir_t=!!(s->flags&SF_LAST_DIR);
		if(dir_t != dir){
			gpio_out_toggle_noirq(s->dir_pin);
		}

	}

	///limite the adjust range
	if(abs_bd(adjusted_step *1000 / step_adj[0].steps_per_mm,0)>RT_RANGE){//>+-0.3mm
		//diff_step = 0;
		if(adjusted_step>0){
		   if (diff_step<0)
		       diff_step=0;
		   //output("adjust_z_move adjusted_step=%c steps_per_mm=%c mm=%c ", abs_bd(adjusted_step,0),step_adj[0].steps_per_mm,abs_bd(adjusted_step *1000 / step_adj[0].steps_per_mm,0));
		}
		else{
		   if (diff_step>0)
		       diff_step=0;
		   //output("adjust_z_move adjusted_step=-%c steps_per_mm=%c mm=%c ", abs_bd(adjusted_step,0),step_adj[0].steps_per_mm,abs_bd(adjusted_step *1000 / step_adj[0].steps_per_mm,0));

		}
		return;
	}

}

#define MAX_Z_PLUSE_TIME  1200
static uint32_t speed_smooth(int32_t adj_len)
{
  static int32_t total_len=0,current_inter=MAX_Z_PLUSE_TIME,acc_count=0;
  int step_now=abs_bd(diff_step,0);
  if (adj_len!=0){
  	total_len = abs_bd(adj_len,0);
	current_inter=MAX_Z_PLUSE_TIME;
    acc_count=0;
  }

  if (total_len <=0)
  	return RT_SAMPLE_TIME;

  if ((total_len-step_now)<(total_len/2)){
     current_inter=current_inter-MAX_Z_PLUSE_TIME/30;
	 if (current_inter<300){
	 	current_inter = 300;
	    return current_inter;
	 }
	 acc_count=step_now;
  }
  else if((total_len-step_now)>(total_len/2)&&(step_now<acc_count)){
     current_inter=current_inter+MAX_Z_PLUSE_TIME/30;
	 if (current_inter>MAX_Z_PLUSE_TIME)
	 	current_inter = MAX_Z_PLUSE_TIME;
  }
  return current_inter;
}

static uint_fast8_t bd_event(struct timer *t)
{

    irq_disable();
    //static uint16_t sensor_z_old = -1;
    uint32_t timer_ilde=RT_SAMPLE_TIME;

     if(diff_step){
		adjust_z_move();
     }
	 else {
		if (BD_read_flag == 1018 && (sda_pin >= 0) && (scl_pin >= 0) &&
		 	(step_adj[0].zoid&&(step_adj[0].cur_z<step_adj[0].adj_z_range)&&e.sample_count==0)){

           	struct stepper *s = stepper_oid_lookup_bd(step_adj[0].zoid);
			if(s->count){
				bd_tim.time.waketime =timer_read_time() + timer_ilde;
				irq_enable();
				return SF_RESCHEDULE;
			}
			uint16_t tm=BD_i2c_read();
			if(tm<1023){
				BD_Data=tm;
				{
					adust_Z_calc(BD_Data,s);
					//sensor_z_old = BD_Data;
				}
			}
			else
				BD_Data=0;
		 }
	 }


     if(e.sample_count || (step_adj[0].cur_z>step_adj[0].adj_z_range) || step_adj[0].adj_z_range==0)
	 	timer_ilde = timer_ilde*10;
	 bd_tim.time.waketime =timer_read_time() + timer_ilde;
     if(diff_step){
	 	//bd_tim.time.waketime =timer_read_time()+timer_from_us(300);
	    bd_tim.time.waketime =timer_read_time()+timer_from_us(speed_smooth(0));
     }
   irq_enable();
   return SF_RESCHEDULE;
}

void timer_bd_init(void)
{
    sched_del_timer(&bd_tim.time);
    bd_tim.time.waketime = timer_read_time()+1000000;
    bd_tim.time.func = bd_event;
    sched_add_timer(&bd_tim.time);
	adjusted_step=0;
	//output("timer_bd_init mcuoid=%c", oid_g);
}

void timer_bd_uinit(void)
{
    sched_del_timer(&bd_tim.time);
	//output("timer_bd_uinit mcuoid=%c", oid_g);
}

void adust_Z_calc(uint16_t sensor_z,struct stepper *s)
{
   // BD_Data
    static int sensor_z_old=0;
    if(step_adj[0].zoid==0 || step_adj[0].adj_z_range<=0
		|| (step_adj[0].cur_z>step_adj[0].adj_z_range)
		|| sensor_z>=380||BD_read_flag!=1018){

		diff_step = 0;
    	return;
	}

	if(s->count){
		//diff_step = 0;
		return;
	}
    int diff_mm = (sensor_z*10 - step_adj[0].cur_z);
    diff_step = diff_mm * step_adj[0].steps_per_mm/1000;
    //nozzle collision detection prected
	if ((sensor_z == sensor_z_old && sensor_z <= 30 && (abs_bd(diff_mm,0)>50))
		||sensor_z <= 5 ){
		diff_step = -10 * step_adj[0].steps_per_mm/1000;
	}
	sensor_z = sensor_z_old;
	speed_smooth(diff_step);
	//output("Z_Move_L mcuoid=%c diff_step=%c sen_z=%c cur_z=%c", oid_g,diff_step>0?diff_step:-diff_step,sensor_z,step_adj[0].cur_z);
    //
	////////////////////////
	return;

}


void
cmd_RT_Live(uint32_t *args)
{
    int cmd=args[1],dat=args[2];
    if(cmd==CMD_Z_INDEX) // 1025  CMD_Z_INDEX
        z_index=dat;
    else if(cmd==CMD_CUR_Z){ //1026 CMD_CUR_Z
        step_adj[0].cur_z=dat;
		diff_step = 0;
    }
    else if(cmd==CMD_ADJ_Z){ //1027  CMD_ADJ_Z
        step_adj[z_index].adj_z_range=dat;
		if (step_adj[0].adj_z_range<50)
			timer_bd_uinit();
	    else if(step_adj[0].adj_z_range<=1000)//(step_adj[0].cur_z<=step_adj[z_index].adj_z_range)
	        timer_bd_init();
    }
    else if(cmd==CMD_DIR_INV) //1028  CMD_DIR_INV
        step_adj[z_index].invert_dir=dat;
    else if(cmd==CMD_STEP_MM){ //1029  CMD_STEP_MM
        step_adj[z_index].steps_per_mm=dat;
    }
    else if(cmd==CMD_ZOID){ //1030  CMD_ZOID
        step_adj[z_index].zoid=dat;
    }
    else if(cmd==CMD_RT_SAMPLE_TIME){ //1031  CMD_SAMPLE_TIME
        RT_SAMPLE_TIME= timer_from_us(dat*1000);;
    }
	else if(cmd==CMD_RT_RANGE){ //1032	CMD_SAMPLE_TIME
		RT_RANGE= dat;
	}

}

void
command_I2C_BD_send(uint32_t *args)
{
    unsigned int cmd_c=args[1];
	oid_g=args[0];
    //output("command_I2C_BD_send mcuoid=%c cmd=%c dat=%c", args[0],cmd_c,args[2]);
	//only read data
	if(cmd_c==CMD_READ_DATA && args[2]==1){

        BD_Data=BD_i2c_read();
        sendf("I2CBDr oid=%c r=%c", oid_g,BD_Data);
	}
	else if(cmd_c<1025&& args[2]==0){
        BD_read_flag=cmd_c;
        BD_i2c_write(cmd_c);
        if(cmd_c>CMD_READ_DATA){
            switch_mode=0;
         }
        sendf("I2CBDr oid=%c r=%c", oid_g,cmd_c);
    }
	else if(cmd_c>=1025 && cmd_c<=1032 ){
		BD_read_flag=cmd_c;
		switch_mode=0;
	    cmd_RT_Live(args);
		sendf("I2CBDr oid=%c r=%c", oid_g,cmd_c);
	}
	else if(cmd_c==1033 ){
		if(e.pin_num!=sda_pin)
			cmd_c = gpio_in_read(e.pin);
		else if (switch_mode == 1){
			cmd_c = gpio_in_read(sda_gpio_in);
		}
		else
			cmd_c = read_endstop_pin();

		sendf("I2CBDr oid=%c r=%c", oid_g,cmd_c);
	}
}

DECL_COMMAND(command_I2C_BD_send, "I2CBD oid=%c c=%c d=%c");


void
command_config_I2C_BD(uint32_t *args)
{
    BD_i2c_init(args[1],args[2],args[3],args[4],args[5],args[6]);
}
DECL_COMMAND(command_config_I2C_BD,
             "config_I2C_BD oid=%c sda_pin=%u scl_pin=%u"
             " delay=%u h_pos=%u z_adjust=%u estop_pin=%u");

int  read_endstop_pin(void)
{
    uint16_t tm;
    tm=BD_i2c_read();
    if(tm<DATA_ERROR)
        BD_Data=tm;
    else
        BD_Data=0;
    if(BD_Data<=homing_pose)
        BD_Data=0;
    return BD_Data?0:1;
}
// Timer callback for an end stop
static uint_fast8_t
endstop_event(struct timer *t)
{
    uint8_t val =0;
    if(e.pin_num!=sda_pin)
        val = gpio_in_read(e.pin);
    else if (switch_mode == 1){
        val = gpio_in_read(sda_gpio_in);
    }
    else
        val = read_endstop_pin();
    uint32_t nextwake = e.time.waketime + e.rest_time;
    if ((val ? ~e.flags : e.flags) & ESF_PIN_HIGH) {
        // No match - reschedule for the next attempt
        e.time.waketime = nextwake;
        return SF_RESCHEDULE;
    }
    e.nextwake = nextwake;
    e.time.func = endstop_oversample_event;
    return endstop_oversample_event(t);
}

// Timer callback for an end stop that is sampling extra times
static uint_fast8_t
endstop_oversample_event(struct timer *t)
{
    uint8_t val =0;
    if(e.pin_num!=sda_pin)
        val = gpio_in_read(e.pin);
    else if (switch_mode == 1){
        val = gpio_in_read(sda_gpio_in);
    }
    else{
        val = BD_Data?0:1;//read_endstop_pin();
    }
    if ((val ? ~e.flags : e.flags) & ESF_PIN_HIGH) {
        // No longer matching - reschedule for the next attempt
        e.time.func = endstop_event;
        e.time.waketime = e.nextwake;
        e.trigger_count = e.sample_count;
        return SF_RESCHEDULE;
    }
    uint8_t count = e.trigger_count - 1;
    if (!count) {
        trsync_do_trigger(e.ts, e.trigger_reason);
	    step_adj[0].adj_z_range=0;
        return SF_DONE;
    }
    e.trigger_count = count;
    e.time.waketime += e.sample_time;
    return SF_RESCHEDULE;
}


// Home an axis
void
command_BDendstop_home(uint32_t *args)
{
    sched_del_timer(&e.time);
    e.time.waketime = args[1];
    e.sample_time = args[2];
    e.sample_count = args[3];
    if (!e.sample_count) {
        // Disable end stop checking
        e.ts = NULL;
        e.flags = 0;
        return;
    }
    e.rest_time = args[4];
    e.time.func = endstop_event;
    e.trigger_count = e.sample_count;
    e.flags = ESF_HOMING | (args[5] ? ESF_PIN_HIGH : 0);
    e.ts = trsync_oid_lookup(args[6]);
    e.trigger_reason = args[7];
    e.pin_num = args[8];
    e.pin =  gpio_in_setup(args[8], 1);
    sched_add_timer(&e.time);
	if(args[9]==1)
	{
        BD_i2c_write(CMD_SWITCH_MODE);
		BD_i2c_write(args[10]);
		sda_gpio_in=gpio_in_setup(sda_pin, 1);
        BD_setLow(scl_gpio);
		switch_mode=1;
	}
}
DECL_COMMAND(command_BDendstop_home,
             "BDendstop_home oid=%c clock=%u sample_ticks=%u sample_count=%c"
             " rest_ticks=%u pin_value=%c trsync_oid=%c trigger_reason=%c"
             " endstop_pin=%c sw=%c sw_val=%c");
