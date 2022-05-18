#include <Arduino.h>
#include <Panda_segmentBed_I2C.h>
I2C_SegmentBED I2CsegmentBED;
#define  I2C_BED_SDA  2  
#define  I2C_BED_SCL  15  
#define DELAY 100
char tmp_1[50];


unsigned short Add_OddEven(unsigned short byte)
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

unsigned short Check_OddEven(unsigned short byte)
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

void   setLow(unsigned char pin) {
      noInterrupts();
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    interrupts();
}

void    setHigh(unsigned char pin) {
     noInterrupts();
      pinMode(pin, INPUT_PULLUP);
     interrupts();
}

void  set_force_High(unsigned char pin) {
     noInterrupts();
      pinMode(pin, OUTPUT);
      digitalWrite(pin, HIGH);
     interrupts();
}


bool I2C_start(void)
{
  setHigh(I2C_BED_SCL);
  setHigh(I2C_BED_SDA);
  delayMicroseconds(DELAY);
  setLow(I2C_BED_SDA);
  delayMicroseconds(DELAY);
  setLow(I2C_BED_SCL);
  delayMicroseconds(DELAY*2);

}
void  i2c_stop(void) {
  delayMicroseconds(DELAY*2);
  setLow(I2C_BED_SDA);
  delayMicroseconds(DELAY);
  setHigh(I2C_BED_SCL);
  delayMicroseconds(DELAY);
  setHigh(I2C_BED_SDA);
  delayMicroseconds(DELAY);
}

unsigned short i2c_read_1(void)
{
   
  I2C_start();
  //// read
  setHigh(I2C_BED_SDA);
  setHigh(I2C_BED_SCL);
  delayMicroseconds(DELAY*2);
  setLow(I2C_BED_SCL);
  ///
  delayMicroseconds(DELAY);
  unsigned short b = 0;
  setHigh(I2C_BED_SDA);
  for (unsigned char i = 0; i <= 10; i++) {
    b <<= 1;
    delayMicroseconds(DELAY);
    setHigh(I2C_BED_SCL);
    delayMicroseconds(DELAY);
    if (digitalRead(I2C_BED_SDA)) b |= 1;
    setLow(I2C_BED_SCL);
  }
  i2c_stop();

  
  return b;

}

void i2c_write_1(unsigned int addr)
{
 
  I2C_start();
  //// write
  setLow(I2C_BED_SDA);
  set_force_High(I2C_BED_SCL);
  delayMicroseconds(DELAY);
  setLow(I2C_BED_SCL);
  addr=Add_OddEven(addr);
  ///write address
  delayMicroseconds(DELAY);
  for (int i=10; i >=0; i--) 
 // for (unsigned int curr = 0x400; curr != 0; curr >>= 1) 
  {
    if ((addr>>i)&0x01) {set_force_High(I2C_BED_SDA);} else  setLow(I2C_BED_SDA); 
    //if (addr &curr) {set_force_High(I2C_BED_SDA);} else  setLow(I2C_BED_SDA); 
    set_force_High(I2C_BED_SCL);
    delayMicroseconds(DELAY);
    setLow(I2C_BED_SCL);
    delayMicroseconds(DELAY);
  }
  ////////////
/* for (unsigned char curr = 0x400; curr != 0; curr >>= 1) {
    if (curr & data) 
       set_force_High(I2C_BED_SDA); 
    else  
       setLow(I2C_BED_SDA); 
    setHigh(I2C_BED_SCL);
    delayMicroseconds(DELAY);
    setLow(I2C_BED_SCL);
    delayMicroseconds(DELAY);
  }
*/


  i2c_stop();
  

}


unsigned int n=0;
void setup() {
  
  delay(1000);
  //I2CsegmentBED.i2c_init(PANDA_BED_SDA,PANDA_BED_SCL,0x3C);
  Serial.begin(115200);

 // digitalWrite(I2C_BED_SDA, LOW);
 // digitalWrite(I2C_BED_SCL, LOW);
  setHigh(I2C_BED_SDA);
  setHigh(I2C_BED_SCL);
  
  n=0;
   
  i2c_write_1(1020);
  i2c_write_1(1020);
  i2c_write_1(1020);
}

void loop() {
  unsigned short tmp=0;
   tmp=i2c_read_1();
    
     sprintf(tmp_1,"read:%d,%d\n",tmp&0x3ff,Check_OddEven(tmp));
     printf(tmp_1);
      delay(100);
    if((tmp&0x3ff)<1020)  
    {
       if(n==10)
      {
        i2c_write_1(1020);
        delay(1000);
      }
     /*
      if(n==30)
        i2c_write_1(1019);
      else if(n<100&&n>30)
        i2c_write_1(n-30);
      if(n==100)
        i2c_write_1(1021); 
        */
      n++;
    }
    else
    {
      i2c_stop();
      delay(500);
      i2c_stop();
    }
  
 

 
}