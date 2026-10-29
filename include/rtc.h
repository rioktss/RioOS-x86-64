#ifndef RTC_H
#define RTC_H

#include "types.h"

/* CMOS RTC (x86-64). */
uint64_t rtc_get_epoch();
int rtc_set_epoch(uint64_t epoch);

#endif
