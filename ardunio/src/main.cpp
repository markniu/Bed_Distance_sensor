#include <Arduino.h>
#include <Panda_segmentBed_I2C.h>
I2C_SegmentBED BD_SENSOR_I2C;
#define  I2C_BED_SDA  2  
#define  I2C_BED_SCL  15  
#define DELAY 100
#define MAX_BD_HEIGHT 6.9
#define CMD_START_READ_CALIBRATE_DATA   1017
#define CMD_END_READ_CALIBRATE_DATA   1018
#define CMD_START_CALIBRATE 1019
#define CMD_END_CALIBRATE 1021  
#define CMD_READ_VERSION  1016

char tmp_1[50];
unsigned int n=0,i=0;
float Distance=0.0;
void setup() {
  delay(500);
  BD_SENSOR_I2C.i2c_init(I2C_BED_SDA,I2C_BED_SCL,0x3C,10);
  Serial.begin(115200);
  n=0;
}

void loop() {
    unsigned short tmp=0;
    tmp=BD_SENSOR_I2C.BD_i2c_read();    
    if(BD_SENSOR_I2C.BD_Check_OddEven(tmp)==0)
      printf("CRC error!\n");
    else
    {
      Distance=(tmp&0x3ff)/100.0;
      sprintf(tmp_1,"Distance:%.2f\n",Distance);
      printf(tmp_1);
    }
    delay(100);
    if((tmp&0x3ff)<1020)  
    {
      /////Read Calibrate data
      if(n==10)
      {
        BD_SENSOR_I2C.BD_i2c_write(CMD_START_READ_CALIBRATE_DATA);
        delay(1000);
        printf("\nRead Calibrate data:");
        for(i=0;i<60;i++)
        {
          tmp=BD_SENSOR_I2C.BD_i2c_read();
          sprintf(tmp_1,"%d,%d,%d\n",i,tmp&0x3ff,BD_SENSOR_I2C.BD_Check_OddEven(tmp));
          printf(tmp_1);
          delay(100);
        }
        printf("\n");
        BD_SENSOR_I2C.BD_i2c_write(CMD_END_READ_CALIBRATE_DATA);


      }
      n++;
    }
    else
    {
      BD_SENSOR_I2C.BD_i2c_stop();
      delay(500);
      BD_SENSOR_I2C.BD_i2c_stop();
    }
  
 

 
}