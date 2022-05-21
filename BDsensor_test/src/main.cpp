#include <Arduino.h>
#include <Panda_segmentBed_I2C.h>
I2C_SegmentBED BD_SENSOR_I2C;
#define  I2C_BED_SDA  2  
#define  I2C_BED_SCL  15  
#define DELAY 100
char tmp_1[50];


unsigned int n=0,i=0;
void setup() {
  
  delay(1000);
  BD_SENSOR_I2C.i2c_init(I2C_BED_SDA,I2C_BED_SCL,0x3C);
  Serial.begin(115200);

 // digitalWrite(I2C_BED_SDA, LOW);
 // digitalWrite(I2C_BED_SCL, LOW);

  
  n=0;
   
  BD_SENSOR_I2C.BD_i2c_write(1020);
  BD_SENSOR_I2C.BD_i2c_write(1020);
  BD_SENSOR_I2C.BD_i2c_write(1020);
}

void loop() {
  unsigned short tmp=0;
   tmp=BD_SENSOR_I2C.BD_i2c_read();
    
     sprintf(tmp_1,"read:%d,%d\n",tmp&0x3ff,BD_SENSOR_I2C.BD_Check_OddEven(tmp));
     printf(tmp_1);
      delay(100);
    if((tmp&0x3ff)<1020)  
    {
       if(n==10)
      {
        BD_SENSOR_I2C.BD_i2c_write(1017);
        delay(1000);
         printf("\nCalibrate data:");
        for(i=0;i<73;i++)
        {
          tmp=BD_SENSOR_I2C.BD_i2c_read();
    
          sprintf(tmp_1,"%d,%d,%d\n",i,tmp&0x3ff,BD_SENSOR_I2C.BD_Check_OddEven(tmp));
          printf(tmp_1);
          delay(500);
        }
         printf("\n");
        BD_SENSOR_I2C.BD_i2c_write(1018);
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
      BD_SENSOR_I2C.BD_i2c_stop();
      delay(500);
      BD_SENSOR_I2C.BD_i2c_stop();
    }
  
 

 
}