//
#ifndef Panda_segmentBed_I2C_h
#define Panda_segmentBed_I2C_h

 #define I2C_READ 1
#define I2C_WRITE 0
//#define DELAY 100 // usec delay
#define BUFFER_LENGTH 32
#define I2C_MAXWAIT 5000
//#define I2C_7BITADDR 0x3C// DS1307
#define MEMLOC 0x0A
#define ADDRLEN 1

class I2C_SegmentBED{
public:
	int i2c_init(unsigned char _sda,unsigned char _scl,unsigned char _addr,int delay_m);
	void I2C_read_str(char *dat_r,int addr);
	void I2C_send_str(char *dat_r,char send_now);
//////////////////////
	void BD_i2c_write(unsigned int addr);
	unsigned short BD_i2c_read(void);
	void  BD_i2c_stop(void);
	bool  BD_I2C_start(void);
	unsigned short BD_Check_OddEven(unsigned short byte);
	unsigned short BD_Add_OddEven(unsigned short byte);

private:
	unsigned char I2C_BED_SDA,I2C_BED_SCL;
	unsigned char I2C_7BITADDR=0x3C;

	unsigned char  _pullup =true ;
	void setLow(unsigned char pin) ;
	void setHigh(unsigned char pin);
	bool i2c_start(unsigned char addr) ;
	bool i2c_rep_start(unsigned char addr);
	void i2c_stop(void);
	bool i2c_write(unsigned char value);
	unsigned char i2c_read(bool last);
//////////////////////
void BD_setLow(unsigned char pin);
void BD_setHigh(unsigned char pin) ;
void BD_set_force_High(unsigned char pin);

};

#endif
