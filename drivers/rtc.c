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
#define RTC_UPDATE_IN_PROGRESS 0x80
#define RTC_HOUR_PM 0x80
#define RTC_24_HOUR 0x02
#define RTC_BINARY_MODE 0x04
#define RTC_WAIT_LIMIT 100000
#define RTC_READ_RETRIES 8

static uint8_t rtc_read(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

static uint8_t rtc_status_b(void) {
    return rtc_read(RTC_STATUSB);
}

static int wait_for_update(void) {
    for (uint32_t i = 0; i < RTC_WAIT_LIMIT; i++) {
        if (!(rtc_read(RTC_STATUSA) & RTC_UPDATE_IN_PROGRESS))
            return 0;
    }
    return -1;
}

static int bcd_to_bin(uint8_t bcd, uint8_t *value) {
    uint8_t low = bcd & 0x0F;
    uint8_t high = bcd >> 4;
    if (low > 9 || high > 9)
        return -1;
    *value = high * 10 + low;
    return 0;
}

static int read_snapshot(rtc_time_t *time, uint8_t *status_b) {
    if (wait_for_update() != 0)
        return -1;

    time->second = rtc_read(RTC_SECOND);
    time->minute = rtc_read(RTC_MINUTE);
    time->hour = rtc_read(RTC_HOUR);
    time->day = rtc_read(RTC_DAY);
    time->month = rtc_read(RTC_MONTH);
    time->year = rtc_read(RTC_YEAR);
    *status_b = rtc_status_b();
    return 0;
}

static int snapshots_match(const rtc_time_t *a, const rtc_time_t *b,
                           uint8_t status_a, uint8_t status_b) {
    return a->second == b->second && a->minute == b->minute &&
           a->hour == b->hour && a->day == b->day &&
           a->month == b->month && a->year == b->year &&
           status_a == status_b;
}

static int decode_snapshot(rtc_time_t *time, uint8_t status_b) {
    int is_24_hour = (status_b & RTC_24_HOUR) != 0;
    int is_binary = (status_b & RTC_BINARY_MODE) != 0;
    int is_pm = !is_24_hour && (time->hour & RTC_HOUR_PM);
    uint8_t year = (uint8_t)time->year;
    time->hour &= 0x7F;

    if (!is_binary &&
        (bcd_to_bin(time->second, &time->second) != 0 ||
         bcd_to_bin(time->minute, &time->minute) != 0 ||
         bcd_to_bin(time->hour, &time->hour) != 0 ||
         bcd_to_bin(time->day, &time->day) != 0 ||
         bcd_to_bin(time->month, &time->month) != 0 ||
         bcd_to_bin(year, &year) != 0))
        return -1;

    if (time->second > 59 || time->minute > 59 ||
        time->month < 1 || time->month > 12 ||
        time->day < 1 || time->day > 31)
        return -1;
    if (is_24_hour) {
        if (time->hour > 23)
            return -1;
    } else {
        if (time->hour < 1 || time->hour > 12)
            return -1;
        time->hour = (time->hour % 12) + (is_pm ? 12 : 0);
    }

    time->year = 2000 + year;
    static const uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    uint8_t days = days_per_month[time->month - 1];
    if (time->month == 2 && time->year % 4 == 0)
        days++;
    if (time->day > days)
        return -1;
    return 0;
}

void rtc_init() {
    uint8_t b = rtc_status_b();
    outb(CMOS_ADDRESS, RTC_STATUSB);
    outb(CMOS_DATA, b | RTC_24_HOUR);
}

int rtc_get_time(rtc_time_t *time) {
    if (!time)
        return -1;

    for (uint32_t attempt = 0; attempt < RTC_READ_RETRIES; attempt++) {
        rtc_time_t first;
        rtc_time_t second;
        uint8_t first_status;
        uint8_t second_status;

        if (read_snapshot(&first, &first_status) != 0 ||
            read_snapshot(&second, &second_status) != 0)
            continue;
        if (!snapshots_match(&first, &second,
                             first_status, second_status))
            continue;
        if (decode_snapshot(&second, second_status) != 0)
            return -1;
        *time = second;
        return 0;
    }
    return -1;
}