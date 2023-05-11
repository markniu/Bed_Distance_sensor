 ////replace endstop.c
#include "autoconf.h"
struct endstop {
    uint8_t type;
    struct timer time;
    struct gpio_in pin;
    uint32_t rest_time, sample_time, nextwake;
    struct trsync *ts;
    uint8_t flags, sample_count, trigger_count, trigger_reason;
};
uint16_t BD_Data=0;
static uint8_t
read_endstop_pin(struct endstop *e)
{
#if CONFIG_MACH_STM32F031
    return gpio_in_read(e->pin);
#else
    if(e->type==2)// for Bed Distance sensor
    {
        return BD_Data?0:1;
    }
    else
        return gpio_in_read(e->pin);
#endif
}
////end replace

