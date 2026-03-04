#ifndef RTC_H
#define RTC_H

#include "types.h"

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint32_t year;
} rtc_time_t;

void     rtc_init();
rtc_time_t rtc_get_time();

#endif // RTC_H