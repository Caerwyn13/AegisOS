#include "rtc.h"
#include "ports.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

#define RTC_SECOND 0x00
#define RTC_MINUTE 0x02
#define RTC_HOUR   0x04
#define RTC_DAY    0x07
#define RTC_MONTH  0x08
#define RTC_YEAR   0x09
#define RTC_STATUSA 0x0A
#define RTC_STATUSB 0x0B

static uint8_t rtc_read(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

static int is_updating() {
    outb(CMOS_ADDRESS, RTC_STATUSA);
    return inb(CMOS_DATA) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

void rtc_init() {
    // disable NMI and set 24 hour mode
    outb(CMOS_ADDRESS, RTC_STATUSB);
    uint8_t b = inb(CMOS_DATA);
    outb(CMOS_ADDRESS, RTC_STATUSB);
    outb(CMOS_DATA, b | 0x02);  // bit 1 = 24 hour mode
}

rtc_time_t rtc_get_time() {
    // wait for RTC update to finish
    while (is_updating());

    rtc_time_t t;
    t.second = rtc_read(RTC_SECOND);
    t.minute = rtc_read(RTC_MINUTE);
    t.hour   = rtc_read(RTC_HOUR);
    t.day    = rtc_read(RTC_DAY);
    t.month  = rtc_read(RTC_MONTH);
    t.year   = rtc_read(RTC_YEAR);

    uint8_t status_b = rtc_read(RTC_STATUSB);

    // convert BCD to binary if needed
    if (!(status_b & 0x04)) {
        t.second = bcd_to_bin(t.second);
        t.minute = bcd_to_bin(t.minute);
        t.hour   = bcd_to_bin(t.hour);
        t.day    = bcd_to_bin(t.day);
        t.month  = bcd_to_bin(t.month);
        t.year   = bcd_to_bin(t.year);
    }

    // year is only two digits from CMOS, assume 2000+
    t.year += 2000;

    return t;
}