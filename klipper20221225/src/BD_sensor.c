#include "autoconf.h" // CONFIG_MACH_AVR
#include "basecmd.h" // oid_alloc
#include "board/gpio.h" // gpio_out_write
#include "board/irq.h" // irq_poll
#include "board/misc.h" // timer_from_us
#include "command.h" // DECL_COMMAND
#include "sched.h" // DECL_SHUTDOWN






uint32_t sda_pin=0,scl_pin=0,delay_m=200;
struct gpio_out sda_gpio, scl_gpio;
struct gpio_in sda_gpio_in;

//#define true 1
//#define false 0

struct bdsensor {
	struct timer timer;
	uint32_t rest_ticks;
	uint16_t sequence, limit_count;
	uint8_t flags, data_count;
	uint8_t data[50];
};

enum {
	AX_HAVE_START = 1<<0, AX_RUNNING = 1<<1, AX_PENDING = 1<<2,
};

static struct task_wake bdsensor_wake;
uint32_t BD_Data=0;


uint32_t Get_Distane_data()
{
	return BD_Data;

}



int BD_i2c_init(uint32_t _sda,uint32_t _scl,uint32_t delays) {
	sda_pin=_sda;
	scl_pin =_scl;
	if(delays>0)
		delay_m=delays;
	sda_gpio=gpio_out_setup(sda_pin, 0);//floting mode
	scl_gpio=gpio_out_setup(scl_pin, 0);
	///
	gpio_out_write(sda_gpio, 1);
	gpio_out_write(scl_gpio, 1);
	return 1;
}
#if 1

uint32_t nsecs_to_ticks_bd(uint32_t ns)
{
    return timer_from_us(ns * 1000) / 1000000;
}

void ndelay_bd(uint32_t nsecs)
{
    if (CONFIG_MACH_AVR)
        // Slower MCUs don't require a delay
        return;
    uint32_t end = timer_read_time() + nsecs_to_ticks_bd(nsecs);
    while (timer_is_before(timer_read_time(), end))
        irq_poll();
        
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



#define BYTE_CHECK_OK     0x01
#define BYTE_CHECK_ERR    0x00
/******************************************************************************************

********************************************************************************************/
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

#define BD_setLow(x) gpio_out_write(x,0)
#define BD_setHigh(x) gpio_out_write(x,1)





void BD_I2C_start(void)
{
	sda_gpio=gpio_out_setup(sda_pin, 0);
	scl_gpio=gpio_out_setup(scl_pin, 0);

	BD_setHigh(scl_gpio);
	BD_setHigh(sda_gpio);
	ndelay_bd(delay_m);
	BD_setLow(sda_gpio);
	ndelay_bd(delay_m);
	BD_setLow(scl_gpio);
	ndelay_bd(delay_m);
	return ;
}
void  BD_i2c_stop(void) {
  ndelay_bd(delay_m);
  sda_gpio=gpio_out_setup(sda_pin, 0);
  BD_setLow(sda_gpio);
  ndelay_bd(delay_m);
  scl_gpio=gpio_out_setup(scl_pin, 0);
  BD_setHigh(scl_gpio);
  ndelay_bd(delay_m);

  BD_setHigh(sda_gpio);
  ndelay_bd(delay_m);
}

unsigned short BD_i2c_read(void)
{
   
  BD_I2C_start();
  //// read
  BD_setHigh(sda_gpio);
  BD_setHigh(scl_gpio);
  ndelay_bd(delay_m);
  BD_setLow(scl_gpio);
  ///
  ndelay_bd(delay_m);
  unsigned short b = 0;
  BD_setHigh(sda_gpio);
  sda_gpio_in=gpio_in_setup(sda_pin, 0);
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

  	  
	if (BD_Check_OddEven(b) && (b & 0x3FF) < 1020)	  
		  // float BD_z = (tmp & 0x3FF) / 100.0f;
		b = (b & 0x3FF);
	 

  if(b>1024)
		b=1024;
  return b;

}

void BD_i2c_write(unsigned int addr)
{
 
  BD_I2C_start();
  //// write
  BD_setLow(sda_gpio);
  BD_setHigh(scl_gpio);
  ndelay_bd(delay_m);
  BD_setLow(scl_gpio);
  addr=BD_Add_OddEven(addr);
  ///write address
  ndelay_bd(delay_m);
  for (int i=10; i >=0; i--) 
  {
    if ((addr>>i)&0x01) {BD_setHigh(sda_gpio);} else  BD_setLow(sda_gpio); 
    //if (addr &curr) {set_force_High(sda_gpio);} else  setLow(sda_gpio); 
    BD_setHigh(scl_gpio);
    ndelay_bd(delay_m);
    BD_setLow(scl_gpio);
    ndelay_bd(delay_m);
  }
  ////////////
  BD_i2c_stop();
  

}

uint32_t INT_to_String(uint32_t BD_z1,uint8_t*data)
{
    uint32_t BD_z=BD_z1;
	//spidev_transfer(spi, 1, data_len, data);
	
	  //sprintf(data,"%.3f",BD_z);
	  uint32_t len=0,j=0; 
	  if(BD_z>1000)
	  {
		  j=BD_z/1000;
		  data[len++] = '0'+j;
		  BD_z-=1000*j;
		  data[len]='0';
		  data[len+1]='0';
		  data[len+2]='0';
	  }
	  if(BD_z>100)
	  {
		  j=BD_z/100;
		  data[len++] = '0'+j;
		  BD_z-=100*j;
		  data[len]='0';
		  data[len+1]='0';
	  }
	  else if(data[len])
		  len++;
	  if(BD_z>10)
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
	return len;
	
}

void 
command_I2C_BD_receive(uint32_t *args)
{
	uint8_t oid = args[0];
	uint8_t data[8];
	 uint32_t BD_z=BD_Data;
	memset(data,0,8);
	
	uint32_t len=0,j=0; 
	if(BD_z>1000)
	{
		j=BD_z/1000;
		data[len++] = '0'+j;
		BD_z-=1000*j;
		data[len]='0';
		data[len+1]='0';
		data[len+2]='0';
	}
	if(BD_z>100)
	{
		j=BD_z/100;
		data[len++] = '0'+j;
		BD_z-=100*j;
		data[len]='0';
		data[len+1]='0';
	}
	else if(data[len])
		len++;
	if(BD_z>10)
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
command_I2C_BD_receive2(uint32_t *args)
{
	uint8_t oid = args[0];
	uint8_t data[8];
	 uint32_t BD_z=BD_Data;
	memset(data,0,8);
	
	uint32_t len=0,j=0; 
	if(BD_z>1000)
	{
		j=BD_z/1000;
		data[len++] = '0'+j;
		BD_z-=1000*j;
		data[len]='0';
		data[len+1]='0';
		data[len+2]='0';
	}
	if(BD_z>100)
	{
		j=BD_z/100;
		data[len++] = '0'+j;
		BD_z-=100*j;
		data[len]='0';
		data[len+1]='0';
	}
	else if(data[len])
		len++;
	if(BD_z>10)
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

	
	sendf("I2C_BD_receive2_response oid=%c response=%*s", oid,len,data); 
}



DECL_COMMAND(command_I2C_BD_receive2, "I2C_BD_receive2 oid=%c data=%*s");



void 
command_I2C_BD_send(uint32_t *args)
{

	// struct spidev_s *spi = spidev_oid_lookup(args[0]);
	//uint8_t data_len = args[1];
	//uint8_t *data = command_decode_ptr(args[2]);
	//spidev_transfer(spi, 0, data_len, data);*/
	uint8_t oid = args[0];
	
	int addr=atoi(args[2]);
	output("mcuoid=%c i2cwrite=%u",oid, addr);
	//sscanf(args[2],"%d",&addr); 
	BD_i2c_write(addr);
	//uint16_t BD_z = BD_i2c_read();	
	//output("mcuoid=%c BD_z=%u", oid,BD_z);
	//output("mcuoid=%c scl_pin=%u delay_m=%u", oid,scl_pin,delay_m);
	sched_wake_task(&bdsensor_wake);
}

DECL_COMMAND(command_I2C_BD_send, "I2C_BD_send oid=%c data=%*s");





// Event handler that wakes adxl345_task() periodically
static uint_fast8_t
bdsensor_event(struct timer *timer)
{
	struct bdsensor *ax = container_of(timer, struct bdsensor, timer);
	ax->flags |= AX_PENDING;
	sched_wake_task(&bdsensor_wake);
	return SF_DONE;
}

void
command_config_I2C_BD(uint32_t *args)
{
    BD_i2c_init(args[1],args[2],args[3]);
    uint8_t mode = args[4];
	
/*	struct bdsensor *ax = oid_alloc(args[0], command_config_I2C_BD
									   , sizeof(*ax));
	ax->timer.func = bdsensor_event;
    // Start new measurements query
    sched_del_timer(&ax->timer);
    ax->timer.waketime = args[1];
    ax->rest_ticks = args[2];
    ax->flags = AX_HAVE_START;
    ax->sequence = ax->limit_count = 0;
    ax->data_count = 0;
    sched_add_timer(&ax->timer);*/


	 
}
DECL_COMMAND(command_config_I2C_BD,
             "config_I2C_BD oid=%c sda_pin=%u scl_pin=%u delay=%u");



 // Helper code to reschedule the bdsensor_event() timer
 static void
 bdsensor_reschedule_timer(struct bdsensor *ax)
 {
	 irq_disable();
	 ax->timer.waketime = timer_read_time() + ax->rest_ticks;
	 sched_add_timer(&ax->timer);
	 irq_enable();
 }
 

 // Startup measurements
 static void
 bdsensor_start(struct bdsensor *ax, uint8_t oid)
 {
	 sched_del_timer(&ax->timer);
	 ax->flags = AX_RUNNING;
	 

	 bdsensor_reschedule_timer(ax);
 }
 
 // End measurements
 static void
 bdsensor_stop(struct bdsensor *ax, uint8_t oid)
 {
	 // Disable measurements
	 sched_del_timer(&ax->timer);
	 ax->flags = 0;

 }


 void
 bd_sensor_task(void)
 {
	// if (!sched_check_wake(&bdsensor_wake))
	//	 return;
	 uint8_t oid;
	 struct bdsensor *ax;
	 foreach_oid(oid, ax, command_config_I2C_BD) {
		// uint_fast8_t flags = ax->flags;
		// if (!(flags & AX_PENDING))
			// continue;
		 //if (flags & AX_HAVE_START)
			// bdsensor_start(ax, oid);
		 //output("mcuoid=%c bdsensortask", oid);
	 }
	if(sda_pin==0||scl_pin==0)
		return;
	uint32_t tm=BD_i2c_read();
	if(tm<1024)
		BD_Data=tm;
	//output("mcuoid=%c bdsensortask", oid);
 }
 DECL_TASK(bd_sensor_task);


#endif
