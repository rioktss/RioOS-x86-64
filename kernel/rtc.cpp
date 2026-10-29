#include "rtc.h"
#ifndef HOST_TEST
#include "x86_io.h"


/* ------------------------------ CMOS RTC --------------------------------- */
static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, (uint8_t)(reg | 0x80U));
    uint8_t value = inb(0x71);
    outb(0x70, 0x00U);
    return value;
}
static void cmos_write(uint8_t reg, uint8_t value)
{
    outb(0x70, (uint8_t)(reg | 0x80U)); outb(0x71, value); outb(0x70, 0x00U);
}
static int64_t civil_days(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (153U * (month + (month > 2 ? (unsigned)-3 : 9U)) + 2U) / 5U + day - 1U;
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}
static void epoch_to_civil(int64_t days, int* year, unsigned* month, unsigned* day)
{
    days += 719468LL;
    int64_t era = (days >= 0 ? days : days - 146096LL) / 146097LL;
    unsigned doe = (unsigned)(days - era * 146097LL);
    unsigned yoe = (doe - doe/1460U + doe/36524U - doe/146096U) / 365U;
    int y = (int)yoe + (int)(era * 400LL);
    unsigned doy = doe - (365U*yoe + yoe/4U - yoe/100U);
    unsigned mp = (5U*doy + 2U)/153U;
    unsigned d = doy - (153U*mp + 2U)/5U + 1U;
    unsigned m = mp + (mp < 10U ? 3U : (unsigned)-9);
    y += m <= 2U;
    *year = y; *month = m; *day = d;
}
static uint8_t bcd_to_bin(uint8_t value) { return (uint8_t)((value & 0x0FU) + ((value >> 4) * 10U)); }
static uint8_t bin_to_bcd(uint8_t value) { return (uint8_t)(((value / 10U) << 4) | (value % 10U)); }
static int rtc_read_fields(int* year, int* month, int* day, int* hour, int* minute, int* second)
{
    for (uint32_t i = 0; i < 100000U && (cmos_read(0x0AU) & 0x80U); ++i) { }
    uint8_t status_b = cmos_read(0x0BU);
    uint8_t sec = cmos_read(0x00U), min = cmos_read(0x02U), hr = cmos_read(0x04U);
    uint8_t date = cmos_read(0x07U), mon = cmos_read(0x08U), yr = cmos_read(0x09U), century = cmos_read(0x32U);
    int pm = (hr & 0x80U) != 0U; hr &= 0x7FU;
    if ((status_b & 0x04U) == 0U)
    {
        sec = bcd_to_bin(sec); min = bcd_to_bin(min); hr = bcd_to_bin(hr);
        date = bcd_to_bin(date); mon = bcd_to_bin(mon); yr = bcd_to_bin(yr); century = bcd_to_bin(century);
    }
    if ((status_b & 0x02U) == 0U) { hr = (uint8_t)(hr % 12U); if (pm) hr = (uint8_t)(hr + 12U); }
    int full_year = century >= 19U && century <= 99U ? (int)century * 100 + (int)yr : (yr >= 70U ? 1900 + yr : 2000 + yr);
    if (mon < 1U || mon > 12U || date < 1U || date > 31U || hr > 23U || min > 59U || sec > 60U) return -1;
    *year = full_year; *month = mon; *day = date; *hour = hr; *minute = min; *second = sec;
    return 0;
}
uint64_t rtc_get_epoch()
{
    int y,m,d,h,mi,s;
    if (rtc_read_fields(&y,&m,&d,&h,&mi,&s) != 0) return 0;
    int64_t days = civil_days(y, (unsigned)m, (unsigned)d);
    if (days < 0) return 0;
    return (uint64_t)days * 86400ULL + (uint64_t)h*3600ULL + (uint64_t)mi*60ULL + (uint64_t)s;
}
int rtc_set_epoch(uint64_t epoch)
{
    int year; unsigned month, day;
    int64_t days = (int64_t)(epoch / 86400ULL);
    epoch_to_civil(days, &year, &month, &day);
    if (year < 1970 || year > 2099) return -1;
    uint8_t hour = (uint8_t)((epoch / 3600ULL) % 24ULL);
    uint8_t minute = (uint8_t)((epoch / 60ULL) % 60ULL);
    uint8_t second = (uint8_t)(epoch % 60ULL);
    uint8_t old_b = cmos_read(0x0BU);
    cmos_write(0x0BU, (uint8_t)(old_b | 0x80U));
    if ((old_b & 0x04U) == 0U)
    {
        second = bin_to_bcd(second); minute = bin_to_bcd(minute); hour = bin_to_bcd(hour);
        day = bin_to_bcd((uint8_t)day); month = bin_to_bcd((uint8_t)month);
    }
    cmos_write(0x00U, second); cmos_write(0x02U, minute); cmos_write(0x04U, hour);
    cmos_write(0x07U, (uint8_t)day); cmos_write(0x08U, (uint8_t)month);
    cmos_write(0x09U, (uint8_t)(year % 100)); cmos_write(0x32U, (uint8_t)(year / 100));
    cmos_write(0x0BU, old_b);
    return 0;
}
#else /* HOST_TEST */
uint64_t rtc_get_epoch() { return 0; }
int rtc_set_epoch(uint64_t) { return -1; }
#endif
