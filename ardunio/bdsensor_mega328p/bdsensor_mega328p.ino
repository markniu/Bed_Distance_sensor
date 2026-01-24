#include <Arduino.h>
#include <U8g2lib.h>
#include "Panda_segmentBed_I2C.h"
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

I2C_SegmentBED BD_SENSOR_I2C;
#define  I2C_BED_SDA  12
#define  I2C_BED_SCL  11
#define DELAY 100
#define MAX_BD_HEIGHT 6.9
#define CMD_START_READ_CALIBRATE_DATA   1017
#define CMD_END_READ_CALIBRATE_DATA   1018
#define CMD_START_CALIBRATE 1019
#define CMD_END_CALIBRATE 1021
#define CMD_READ_VERSION  1016
#define CMD_DISTANCD_RAWDATA_TYPE 1020       // switch output distance data type to raw data, default is mm (1 represent 0.01mm)

char tmp_1[50];
unsigned int n=0,i=0;
float Distance=0.0;

U8G2_SSD1306_128X32_UNIVISION_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);



void dis_text(char *tmp_d,char x,char y)
{

  u8g2.setFont(u8g2_font_ncenB08_tr); // choose a suitable font
  u8g2.drawStr(x,y,tmp_d);  // write something to the internal memory
  u8g2.sendBuffer();          // transfer internal memory to the display

}

unsigned short read_one_data()
{
    unsigned short tmp=0;
    tmp=BD_SENSOR_I2C.BD_i2c_read();
    if(BD_SENSOR_I2C.BD_Check_OddEven(tmp)==0)
      //printf("CRC error!\n");
     // sprintf(tmp_1,"CRC error");
       tmp=1024;
    else
    {
      tmp=(tmp&0x3ff);
     // sprintf(tmp_1,"Distance:%.2f\n",Distance);
      //printf(tmp_1);
    }
    return tmp;
}

void read_version(char *txt)
{
  int i=0;
  BD_SENSOR_I2C.BD_i2c_write(CMD_READ_VERSION);
  for(i=0;i<20;i++)
      txt[i]=read_one_data();
  BD_SENSOR_I2C.BD_i2c_write(CMD_END_READ_CALIBRATE_DATA);
}


unsigned short read_raw_data(void)
{
  unsigned short tm=0;
  BD_SENSOR_I2C.BD_i2c_write(CMD_DISTANCD_RAWDATA_TYPE);
  tm=read_one_data();
  BD_SENSOR_I2C.BD_i2c_write(CMD_END_READ_CALIBRATE_DATA);
  return tm;
}

void setup(void) {
  u8g2.begin();
  u8g2.clearBuffer();          // clear the internal memory
  BD_SENSOR_I2C.i2c_init(I2C_BED_SDA,I2C_BED_SCL,0x3C,20);
  BD_SENSOR_I2C.BD_i2c_write(CMD_END_READ_CALIBRATE_DATA);
   //read and display version
  sprintf(tmp_1,"");
  read_version(tmp_1+strlen(tmp_1));
  dis_text(tmp_1,0,10);
  ///

}

void loop(void) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr); // choose a suitable font
  //u8g2.drawStr(x,y,tmp_d);  // write something to the internal memory
  //u8g2.sendBuffer();

  sprintf(tmp_1,"");
  read_version(tmp_1+strlen(tmp_1));
  //dis_text(tmp_1,0,10);
  u8g2.drawStr(0,10,tmp_1);

  //delay(10);
  //read and display raw data
  n=read_raw_data();
  sprintf(tmp_1,"%d ",n);
  //dis_text(tmp_1,0,30);

  // read and display distance,but need to calibrate first
  Distance=read_one_data()/100.0;
  sprintf(tmp_1+strlen(tmp_1),", ");
  dtostrf(Distance, 5, 2, tmp_1+strlen(tmp_1));
  sprintf(tmp_1+strlen(tmp_1)," mm");
  u8g2.drawStr(0,30,tmp_1);
  u8g2.sendBuffer();
  //dis_text(tmp_1,0,30);

}
