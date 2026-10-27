#include "storage.h"
#include "keyboard.h"
#include "net_device.h"
#include "rtc.h"
#include "uart.h"
#include "input.h"
#include "fb.h"

/* Native PC platform backends. QEMU is configured with a legacy IDE disk and
 * PS/2 input; network intentionally remains unavailable until a PCI NIC driver
 * is selected and hardened. */
static inline void outb(uint16_t port, uint8_t value)
{ asm volatile("outb %0, %1" :: "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port)
{ uint8_t value; asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static inline void outw(uint16_t port, uint16_t value)
{ asm volatile("outw %0, %1" :: "a"(value), "Nd"(port)); }
static inline uint16_t inw(uint16_t port)
{ uint16_t value; asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port)); return value; }

/* -------------------------- ATA PIO primary master ----------------------- */
#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT0  0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_HDDEVSEL   0x1F6
#define ATA_COMMAND    0x1F7
#define ATA_STATUS     0x1F7
#define ATA_ALTSTATUS  0x3F6
#define ATA_CONTROL    0x3F6
#define ATA_SR_BSY     0x80
#define ATA_SR_DRDY    0x40
#define ATA_SR_DF      0x20
#define ATA_SR_DRQ     0x08
#define ATA_SR_ERR     0x01
static int ata_online = 0;
static uint64_t ata_sectors = 0;

static void ata_delay_400ns()
{
    (void)inb(ATA_ALTSTATUS); (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS); (void)inb(ATA_ALTSTATUS);
}

static int ata_wait_ready(int require_drq)
{
    for (uint32_t i = 0; i < 2000000U; ++i)
    {
        uint8_t status = inb(ATA_STATUS);
        if (status == 0xFFU || status == 0U) return -1;
        if ((status & ATA_SR_BSY) != 0U) continue;
        if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0U) return -1;
        if (!require_drq || (status & ATA_SR_DRQ) != 0U) return 0;
    }
    return -1;
}

static int ata_select(uint64_t sector)
{
    if (sector >= 0x10000000ULL) return -1; /* LBA28 */
    outb(ATA_HDDEVSEL, (uint8_t)(0xE0U | ((sector >> 24) & 0x0FU)));
    ata_delay_400ns();
    outb(ATA_SECCOUNT0, 1);
    outb(ATA_LBA0, (uint8_t)sector);
    outb(ATA_LBA1, (uint8_t)(sector >> 8));
    outb(ATA_LBA2, (uint8_t)(sector >> 16));
    return 0;
}

void storage_init()
{
    ata_online = 0;
    ata_sectors = 0;
    outb(ATA_CONTROL, 0x02U); /* Disable ATA device IRQs; all I/O is polled. */
    outb(ATA_HDDEVSEL, 0xA0U);
    ata_delay_400ns();
    outb(ATA_SECCOUNT0, 0);
    outb(ATA_LBA0, 0); outb(ATA_LBA1, 0); outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, 0xECU); /* IDENTIFY DEVICE */
    uint8_t status = inb(ATA_STATUS);
    if (status == 0U || status == 0xFFU)
    {
        uart_puts("Storage: no ATA primary-master disk detected; using RAM filesystem.\r\n");
        return;
    }
    if (ata_wait_ready(1) != 0)
    {
        uart_puts("Storage: ATA IDENTIFY failed; using RAM filesystem.\r\n");
        return;
    }
    uint16_t identify[256];
    for (int i = 0; i < 256; ++i) identify[i] = inw(ATA_DATA);
    uint64_t sectors28 = (uint64_t)identify[60] | ((uint64_t)identify[61] << 16);
    uint64_t sectors48 = (uint64_t)identify[100] | ((uint64_t)identify[101] << 16) |
                         ((uint64_t)identify[102] << 32) | ((uint64_t)identify[103] << 48);
    ata_sectors = sectors48 ? sectors48 : sectors28;
    if (ata_sectors == 0) ata_sectors = sectors28;
    ata_online = ata_sectors != 0;
    if (ata_online)
    {
        uart_puts("Storage: ATA PIO primary master ready; sectors = ");
        char text[24]; int n = 0; uint64_t value = ata_sectors;
        do { text[n++] = (char)('0' + value % 10U); value /= 10U; } while (value && n < 23);
        while (n) uart_putc(text[--n]);
        uart_puts(".\r\n");
    }
    else uart_puts("Storage: ATA disk reported no addressable sectors; RAM filesystem selected.\r\n");
}

int storage_ready() { return ata_online; }
int storage_read_sector(uint64_t sector, void* buffer)
{
    if (!ata_online || !buffer || sector >= ata_sectors || ata_select(sector) != 0) return -1;
    outb(ATA_COMMAND, 0x20U);
    if (ata_wait_ready(1) != 0) return -1;
    uint16_t* out = (uint16_t*)buffer;
    for (int i = 0; i < 256; ++i) out[i] = inw(ATA_DATA);
    return 0;
}
int storage_write_sector(uint64_t sector, const void* buffer)
{
    if (!ata_online || !buffer || sector >= ata_sectors || ata_select(sector) != 0) return -1;
    outb(ATA_COMMAND, 0x30U);
    if (ata_wait_ready(1) != 0) return -1;
    const uint16_t* in = (const uint16_t*)buffer;
    for (int i = 0; i < 256; ++i) outw(ATA_DATA, in[i]);
    ata_delay_400ns();
    return ata_wait_ready(0);
}
int storage_flush()
{
    if (!ata_online) return -1;
    outb(ATA_COMMAND, 0xE7U);
    return ata_wait_ready(0);
}
void storage_status()
{
    if (ata_online) uart_puts("Storage: ATA PIO primary master (LBA28/LBA48 identify, polling).\r\n");
    else uart_puts("Storage offline; filesystem uses RAM fallback.\r\n");
}

/* ---------------------- PS/2 keyboard + mouse input ---------------------- */
/* One shared decoder drains the controller output buffer (status bit 5 tells
   keyboard from aux/mouse bytes) into an event queue. Keys use the same codes
   as the AArch64 build: Ctrl+letter = 1..26, arrows = 0x80 up / 0x81 down /
   0x82 left / 0x83 right. The mouse is relative on PS/2, so it is integrated
   into an absolute 0..32767 pointer position. */
static int keyboard_online = 0;
static int mouse_online = 0;
static int shift_down = 0;
static int ctrl_down = 0;
static int caps_lock = 0;
static int extended_key = 0;
static int keyboard_ready = 0;

#define EVQ_SIZE 128
static RioInputEvent evq[EVQ_SIZE];
static uint32_t evq_head = 0, evq_tail = 0;
static uint8_t mouse_packet[3];
static int mouse_index = 0;
static int mouse_x = 0, mouse_y = 0, mouse_buttons = 0;

static void ps2_wait_input_clear()
{
    for (uint32_t i = 0; i < 100000U; ++i) if ((inb(0x64) & 0x02U) == 0U) return;
}
static int ps2_wait_output_ready()
{
    for (uint32_t i = 0; i < 100000U; ++i) if ((inb(0x64) & 0x01U) != 0U) return 1;
    return 0;
}
static void ps2_command(uint8_t cmd) { ps2_wait_input_clear(); outb(0x64, cmd); }
static void ps2_data(uint8_t value) { ps2_wait_input_clear(); outb(0x60, value); }

static void ev_push(const RioInputEvent& e)
{
    uint32_t next = (evq_head + 1U) % EVQ_SIZE;
    if (next == evq_tail) return; /* full: drop newest */
    evq[evq_head] = e;
    evq_head = next;
}
static void ev_push_key(char c)
{
    RioInputEvent e; e.kind = RIO_INPUT_KEY; e.key = c; e.x = 0; e.y = 0; e.pressed = 1;
    ev_push(e);
}

static char translate_scancode(uint8_t code, int shifted)
{
    static const char normal[128] = {
        0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
        'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
        'b','n','m',',','.','/',0,'*',0,' ',0
    };
    static const char upper[128] = {
        0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
        'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
        'B','N','M','<','>','?',0,'*',0,' ',0
    };
    if (code >= 128U) return 0;
    char c = shifted ? upper[code] : normal[code];
    if (caps_lock && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    else if (caps_lock && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return c;
}

static void kbd_byte(uint8_t scan)
{
    if (scan == 0xE0U) { extended_key = 1; return; }
    int ext = extended_key; extended_key = 0;
    if (scan == 0x1DU) { ctrl_down = 1; return; }        /* left/right Ctrl make */
    if (scan == 0x9DU) { ctrl_down = 0; return; }
    if (!ext && (scan == 0x2AU || scan == 0x36U)) { shift_down = 1; return; }
    if (!ext && (scan == 0xAAU || scan == 0xB6U)) { shift_down = 0; return; }
    if (scan == 0x3AU) { caps_lock = !caps_lock; return; }
    if ((scan & 0x80U) != 0U) return;                     /* other releases */
    if (ext)
    {
        char k = 0;
        if (scan == 0x48U) k = (char)0x80;                /* up    */
        else if (scan == 0x50U) k = (char)0x81;           /* down  */
        else if (scan == 0x4BU) k = (char)0x82;           /* left  */
        else if (scan == 0x4DU) k = (char)0x83;           /* right */
        else if (scan == 0x1CU) k = '\n';                 /* keypad enter */
        else if (scan == 0x53U) k = 127;                  /* delete */
        if (k) ev_push_key(k);
        return;
    }
    char c = translate_scancode(scan, shift_down);
    if (!c) return;
    if (ctrl_down)
    {
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
        else if (c == '[') c = 27;
        else if (c == ' ') c = 0;
        if (c == 0) return;
    }
    ev_push_key(c);
}

static void mouse_byte(uint8_t b)
{
    if (!mouse_online) return;
    if (mouse_index == 0 && (b & 0x08U) == 0U) return;   /* resync on flags byte */
    mouse_packet[mouse_index++] = b;
    if (mouse_index < 3) return;
    mouse_index = 0;
    uint8_t flags = mouse_packet[0];
    if (flags & 0xC0U) return;                            /* overflow: drop packet */
    int dx = (int)mouse_packet[1] - ((flags & 0x10U) ? 256 : 0);
    int dy = (int)mouse_packet[2] - ((flags & 0x20U) ? 256 : 0);
    int w = fb_get_width(), h = fb_get_height();
    if (w < 2) w = 1024;
    if (h < 2) h = 768;
    mouse_x += dx; mouse_y -= dy;                         /* PS/2 Y is up-positive */
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x > w - 1) mouse_x = w - 1;
    if (mouse_y > h - 1) mouse_y = h - 1;
    mouse_buttons = flags & 1;
    RioInputEvent e;
    e.kind = RIO_INPUT_POINTER; e.key = 0;
    e.x = (int32_t)(((int64_t)mouse_x * 32767) / (w - 1));
    e.y = (int32_t)(((int64_t)mouse_y * 32767) / (h - 1));
    e.pressed = (uint32_t)mouse_buttons;
    ev_push(e);
}

static void ps2_pump()
{
    for (int n = 0; n < 64; ++n)
    {
        uint8_t status = inb(0x64);
        if ((status & 0x01U) == 0U) return;
        uint8_t byte = inb(0x60);
        if (status & 0x20U) mouse_byte(byte);
        else kbd_byte(byte);
    }
}

static int keyboard_read_char(char* out)
{
    if (!out) return 0;
    ps2_pump();
    while (evq_tail != evq_head)
    {
        RioInputEvent e = evq[evq_tail];
        evq_tail = (evq_tail + 1U) % EVQ_SIZE;
        if (e.kind == RIO_INPUT_KEY) { *out = e.key; return 1; }
    }
    return 0;
}

static int mouse_command(uint8_t value)
{
    ps2_command(0xD4U);
    ps2_data(value);
    for (int i = 0; i < 4; ++i)
    {
        if (!ps2_wait_output_ready()) return 0;
        uint8_t r = inb(0x60);
        if (r == 0xFAU) return 1;
    }
    return 0;
}

void keyboard_init()
{
    keyboard_online = 0; mouse_online = 0;
    evq_head = evq_tail = 0; mouse_index = 0;
    /* Enable PS/2 keyboard and mouse while keeping controller interrupts off (polling). */
    ps2_command(0xADU); /* disable keyboard while setting controller state */
    ps2_command(0x20U);
    uint8_t command_byte = 0;
    if (ps2_wait_output_ready()) command_byte = inb(0x60);
    command_byte |= 0x40U; /* Set 1 translation. */
    command_byte &= (uint8_t)~0x03U; /* IRQ1/IRQ12 masked: polling mode. */
    command_byte &= (uint8_t)~0x30U; /* both port clocks enabled */
    ps2_command(0x60U); ps2_data(command_byte);
    ps2_command(0xAEU);
    ps2_command(0xA8U); /* enable aux (mouse) port */
    ps2_wait_input_clear(); outb(0x60, 0xF4U);
    if (ps2_wait_output_ready()) (void)inb(0x60); /* ACK, if present. */
    keyboard_online = 1;
    keyboard_ready = 1;
    mouse_x = 512; mouse_y = 384;
    if (mouse_command(0xF6U) && mouse_command(0xF4U)) mouse_online = 1;
    if (mouse_online)
        uart_puts("Input: PS/2 keyboard + mouse polling enabled; COM1 remains available.\r\n");
    else
        uart_puts("Input: PS/2 keyboard polling enabled (no mouse); COM1 remains available.\r\n");
}
char keyboard_getc()
{
    char c = 0;
    while (!keyboard_read_char(&c)) asm volatile("pause");
    return c;
}
int keyboard_has_data()
{
    ps2_pump();
    /* Text consumers only care about keys: discard queued pointer events. */
    while (evq_tail != evq_head && evq[evq_tail].kind != RIO_INPUT_KEY)
        evq_tail = (evq_tail + 1U) % EVQ_SIZE;
    return evq_tail != evq_head;
}
int keyboard_try_getc(char* out) { return keyboard_read_char(out); }

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

/* Keep the UI's transport-neutral input API; on PC it is backed by PS/2. */
void input_init() { if (!keyboard_ready) keyboard_init(); }
int input_ready() { return keyboard_online; }
int input_keyboard_ready() { return keyboard_online; }
int input_pointer_ready() { return mouse_online; }
int input_poll(RioInputEvent* event)
{
    if (!event) return 0;
    event->kind = RIO_INPUT_NONE; event->key = 0; event->x = 0; event->y = 0; event->pressed = 0;
    ps2_pump();
    if (evq_tail == evq_head) return 0;
    *event = evq[evq_tail];
    evq_tail = (evq_tail + 1U) % EVQ_SIZE;
    return 1;
}
int input_try_get_key(char* out) { return keyboard_read_char(out); }
