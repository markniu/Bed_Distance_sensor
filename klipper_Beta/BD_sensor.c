
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
#include "sched.h" // struct timer
#include "stepper.h" // stepper_event
#include "trsync.h" // trsync_add_signal


#define BYTE_CHECK_OK     0x01
#define BYTE_CHECK_ERR    0x00
#define BD_setLow(x)  gpio_out_write(x,0)
#define BD_setHigh(x) gpio_out_write(x,1)


uint32_t sda_pin=0,scl_pin=0,delay_m=20,homing_pose=0;
int z_ofset=0;
uint16_t BD_Data;
//extern uint32_t timer_period_time;
uint16_t BD_read_flag=1018,BD_read_lock=0;

struct gpio_out sda_gpio, scl_gpio;
struct gpio_in sda_gpio_in;
uint8_t oid_g,etrsync_oid,endstop_reason=0;
uint8_t z_oid[4];
uint32_t endtime_adjust=0;
uint32_t endtime_debug=0;
uint32_t timer_period_endstop=100;

///////////BDsensor as endstop
struct timer time_bd;
#include "autoconf.h"
struct endstop {
    struct timer time;
    uint32_t rest_time, sample_time, nextwake;
    struct trsync *ts;
    uint8_t flags, sample_count, trigger_count, trigger_reason;
};

#define read_endstop_pin() BD_Data?0:1

enum { ESF_PIN_HIGH=1<<0, ESF_HOMING=1<<1 };
static uint_fast8_t endstop_oversample_event(struct timer *t);
struct endstop e ;
///////////////

uint16_t BD_i2c_read(void);
enum { POSITION_BIAS=0x40000000 };

struct stepper_move {
    struct move_node node;
    uint32_t interval;
    int16_t add;
    uint16_t count;
    uint8_t flags;
};

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

int BD_i2c_init(uint32_t _sda,uint32_t _scl,uint32_t delays,uint32_t h_pose,int z_adjust)
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
		if(BD_read_flag==1018&&(b<1000)){
			b = b + z_ofset;
		}

    }
	else
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

enum {
    SF_LAST_DIR=1<<0, SF_NEXT_DIR=1<<1, SF_INVERT_STEP=1<<2, SF_NEED_RESET=1<<3,
    SF_SINGLE_SCHED=1<<4, SF_HAVE_ADD=1<<5
};
	enum { MF_DIR=1<<0 };

extern   uint32_t
stepper_get_position(struct stepper *s);
extern   struct stepper *
stepper_oid_lookup(uint8_t oid);
#if CONFIG_INLINE_STEPPER_HACK && CONFIG_HAVE_STEPPER_BOTH_EDGE
 #define HAVE_SINGLE_SCHEDULE 1
 #define HAVE_EDGE_OPTIMIZATION 1
 #define HAVE_AVR_OPTIMIZATION 0
 DECL_CONSTANT("STEPPER_BOTH_EDGE", 1);
#elif CONFIG_INLINE_STEPPER_HACK && CONFIG_MACH_AVR
 #define HAVE_SINGLE_SCHEDULE 1
 #define HAVE_EDGE_OPTIMIZATION 0
 #define HAVE_AVR_OPTIMIZATION 1
#else
 #define HAVE_SINGLE_SCHEDULE 0
 #define HAVE_EDGE_OPTIMIZATION 0
 #define HAVE_AVR_OPTIMIZATION 0
#endif

void stepper_load_next_1(struct stepper *s)
{
    if (move_queue_empty(&s->mq)) {
        // There is no next move - the queue is empty
        s->count = 0;
        return SF_DONE;
    }

    // Load next 'struct stepper_move' into 'struct stepper'
    struct move_node *mn = move_queue_pop(&s->mq);
    struct stepper_move *m = container_of(mn, struct stepper_move, node);
    s->add = m->add;
    s->interval = m->interval + m->add;
	output("stepper_load mcuoid=%c flag0=%c", oid_g,s->flags);
    if (HAVE_SINGLE_SCHEDULE && s->flags & SF_SINGLE_SCHED) {
        s->time.waketime += m->interval;
        if (HAVE_AVR_OPTIMIZATION)
            s->flags = m->add ? s->flags|SF_HAVE_ADD : s->flags & ~SF_HAVE_ADD;
        s->count = m->count;
		output("stepper_load mcuoid=%c flag=%c", oid_g,s->flags);
    } else {
        // It is necessary to schedule unstep events and so there are
        // twice as many events.
         
		s->next_step_time += m->interval;
		if(s->next_step_time<timer_read_time()+m->interval)	
        	s->next_step_time = timer_read_time()+m->interval;
        s->time.waketime = s->next_step_time;
        s->count = (uint32_t)m->count;// * 2;
		output("stepper_load_else mcuoid=%c count=%c", oid_g,s->count);
    }
    // Add all steps to s->position (stepper_get_position() can calc mid-move)
    if (m->flags & MF_DIR) {
        s->position = -s->position + m->count;
        gpio_out_toggle_noirq(s->dir_pin);
    } else {
        s->position += m->count;
    }

    move_free(m);
    return SF_RESCHEDULE;
}


void adust_Z_live(uint16_t sensor_z)
{
   // BD_Data
    if(step_adj[0].zoid==0)
       return;
	sensor_z=BD_i2c_read();
    struct stepper *s = stepper_oid_lookup(step_adj[0].zoid);
    uint32_t cur_stp=stepper_get_position(s);
    output("Z_Move_L mcuoid=%c cur_step=%c",
        oid_g,(cur_stp-step_adj[0].steps_at_zero));
   /// if((step_adj[0].adj_z_range*step_adj[0].steps_per_mm/10)
    ///    <=(cur_stp-step_adj[0].steps_at_zero))
    ///    return;
    if(sensor_z>=390)
        return;//out of range
    int diff_step=sensor_z*step_adj[0].steps_per_mm/100
        -(cur_stp-step_adj[0].steps_at_zero);
    int dir=0;
   // int old_dir=gpio_output_data_read(s->dir_pin);
    if(diff_step<0)
    {
        diff_step=-diff_step;
        dir=1;
    }
    output("Z_Move_L mcuoid=%c diff_step=%c sensor_z=%c", oid_g,diff_step,sensor_z);

/*
    for(int ii=0;ii<diff_step;ii++)
    {
        int nn=0;
        if(ii<diff_step/2)
            nn= diff_step/2-ii;
        else
            nn=ii-diff_step/2;
        gpio_out_write(s->step_pin, 1);
        ndelay_bd(step_adj[0].step_time*100000*nn);
        gpio_out_write(s->step_pin, 0);
        ndelay_bd(step_adj[0].step_time*100000*nn);
    }
    gpio_out_write(s->dir_pin, old_dir);
    */
   // s->count=diff_step;
    if(s->count)
		 return;
	
	struct stepper_move *m = move_alloc();
    m->interval = 100000;//args[1];
    m->count = diff_step;//args[2];
    if (!m->count)
		return;
	
    if(step_adj[0].invert_dir==1)
        dir=~dir;
    
	
    m->add = 0;//args[3];
    m->flags  = 0;
    if(dir==1)
        //gpio_out_write(s->dir_pin, 1);
        m->flags |=MF_DIR;

    irq_disable();
    uint8_t flags = s->flags;
    if (!!(flags & SF_LAST_DIR) != !!(flags & SF_NEXT_DIR)) {
        flags ^= SF_LAST_DIR;
        m->flags |= MF_DIR;
    }
    if (flags & SF_NEED_RESET) {
		output("Z_Move0 mcuoid=%c FLAGES=%c", oid_g,flags);
        move_free(m);
    } else {
		output("Z_Move1 mcuoid=%c else=%c", oid_g,s->flags);
        s->flags = flags;
		//s->time.waketime=timer_read_time()+100000;
        move_queue_push(&m->node, &s->mq);
		s->time.waketime=timer_read_time()+100000;
        stepper_load_next_1(s);
		//s->time.waketime=timer_read_time()+100000;
        sched_add_timer(&s->time);
    }
    irq_enable();
}



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
        step_adj[0].invert_dir=j;
    else if(tmp[0]=='4')
    {
        step_adj[0].steps_per_mm=j;
    }
    else if(tmp[0]=='5')
        step_adj[0].step_time=j;
    else if(tmp[0]=='6')
    {
        struct stepper *s = stepper_oid_lookup(j);
        uint32_t cur_stp=stepper_get_position(s);
        step_adj[0].zoid=j;
        step_adj[0].steps_at_zero=
            cur_stp-(step_adj[0].steps_at_zero*step_adj[0].steps_per_mm)/1000;
        output("Z_Move_L mcuoid=%c zero=%c", oid,step_adj[0].steps_at_zero);
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
    {
        stepx_probe.xoid=j;
        struct stepper *s = stepper_oid_lookup(j);
        uint32_t cur_stp=stepper_get_position(s);
        stepx_probe.steps_at_zero=cur_stp-
            (stepx_probe.steps_at_zero*stepx_probe.steps_per_mm)/1000;
        stepx_probe.min_x=stepx_probe.min_x*stepx_probe.steps_per_mm
            +stepx_probe.steps_per_mm;
        stepx_probe.max_x=stepx_probe.max_x*stepx_probe.steps_per_mm
            -stepx_probe.steps_per_mm;
      //  output("Z_Move_L mcuoid=%c zero=%c", oid,stepx_probe.max_x);
        stepx_probe.x_count=0;
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
        stepx_probe.y_oid=j;
        struct stepper *s = stepper_oid_lookup(j);
        uint32_t cur_stp=stepper_get_position(s);
        stepx_probe.y_steps_at_zero=cur_stp-
            (stepx_probe.y_steps_at_zero*stepx_probe.y_steps_per_mm)/1000;
    }

   output("Z_Move_L mcuoid=%c j=%c", oid,j);

    sendf("Z_Move_Live_response oid=%c return_set=%*s", oid,i,(char *)args[2]);
}
DECL_COMMAND(command_Z_Move_Live, "Z_Move_Live oid=%c data=%*s");

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

//"Z_Move_Live oid=%c z_oid=%c dir=%u step=%u delay=%u"

void
command_config_BDendstop(uint32_t *args)
{
    struct endstop *e = oid_alloc(args[0], command_config_BDendstop, sizeof(*e));
   // e.pin = gpio_in_setup(args[1], args[2]);
   // e.type = args[2];
}



void
command_config_I2C_BD(uint32_t *args)
{
    oid_g = args[0];
    BD_i2c_init(args[1],args[2],args[3],args[4],args[5]);
	command_config_BDendstop(args);
}
DECL_COMMAND(command_config_I2C_BD,
             "config_I2C_BD oid=%c sda_pin=%u scl_pin=%u delay=%u h_pos=%u z_adjust=%u");


 void
 bd_sensor_task(void)
 {

    //uint32_t len=0;
    uint16_t tm;
	static unsigned int ky=0,time_next=0;
	ky++;


    if(BD_read_flag!=1018)
        return;

    if(sda_pin==0||scl_pin==0)
        return;
   // if(e.sample_count==0)
	//	return;    
    
    tm=BD_i2c_read();
    if(tm<1023)
    {
        BD_Data=tm;
		uint32_t now = timer_read_time();
    	if(now > time_next)
    	{
			time_next=now + 1000000;
			if(tm>10)
			  adust_Z_live(tm);
		}

    }
	else
		BD_Data=0;

    if(BD_Data<=homing_pose)
    {
		BD_Data=0;
		
    }

 }
 DECL_TASK(bd_sensor_task);


// Timer callback for an end stop
static uint_fast8_t
endstop_event(struct timer *t)
{
 //   struct endstop *e = container_of(t, struct endstop, time);
    uint8_t val = read_endstop_pin();
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
   // struct endstop *e = container_of(t, struct endstop, time);
    uint8_t val = read_endstop_pin();
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
    //struct endstop *e = oid_lookup(args[0], command_config_BDendstop);
    endtime_adjust=0;
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
    sched_add_timer(&e.time);
}
DECL_COMMAND(command_BDendstop_home,
             "BDendstop_home oid=%c clock=%u sample_ticks=%u sample_count=%c"
             " rest_ticks=%u pin_value=%c trsync_oid=%c trigger_reason=%c");
