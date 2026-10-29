#include "keyboard.h"
#include "uart.h"
#include "console.h"
#include "interrupt.h"
#include "x86_io.h"

/* PS/2 keyboard + mouse wheel, polled (controller IRQs stay masked).
   Key codes match the AArch64 build: Ctrl+letter = 1..26, arrows =
   0x80 up / 0x81 down / 0x82 left / 0x83 right. PgUp/PgDn and the mouse
   wheel scroll the QEMU-window terminal and are not delivered as keys. */
#define WHEEL_LINES_PER_NOTCH 3
#define KQ_SIZE 128

static int keyboard_online = 0;
static int mouse_online = 0;
static int mouse_has_wheel = 0;
static int shift_down = 0;
static int ctrl_down = 0;
static int caps_lock = 0;
static int extended_key = 0;
static uint8_t kq[KQ_SIZE];
static uint32_t kq_head = 0, kq_tail = 0;
static uint8_t mouse_packet[4];
static int mouse_index = 0;

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

static void kq_push(char c)
{
    uint32_t next = (kq_head + 1U) % KQ_SIZE;
    if (next == kq_tail) return; /* full: drop newest */
    kq[kq_head] = (uint8_t)c;
    kq_head = next;
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
    if (scan == 0x1DU) { ctrl_down = 1; return; }        /* Ctrl make */
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
        else if (scan == 0x49U) { console_scroll_pages(1); return; }   /* PgUp */
        else if (scan == 0x51U) { console_scroll_pages(-1); return; }  /* PgDn */
        if (k) kq_push(k);
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
    kq_push(c);
}

static void mouse_byte(uint8_t b)
{
    if (!mouse_online) return;
    int size = mouse_has_wheel ? 4 : 3;
    if (mouse_index == 0 && (b & 0x08U) == 0U) return;   /* resync on flags byte */
    mouse_packet[mouse_index++] = b;
    if (mouse_index < size) return;
    mouse_index = 0;
    if (!mouse_has_wheel) return;
    if (mouse_packet[0] & 0xC0U) return;                  /* overflow: drop packet */
    int z = (int)(signed char)mouse_packet[3];                 /* signed; + = wheel down */
    if (z > 12) z = 12;
    if (z < -12) z = -12;
    if (z != 0) console_scroll_lines(-z * WHEEL_LINES_PER_NOTCH);
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
    keyboard_online = 0; mouse_online = 0; mouse_has_wheel = 0;
    kq_head = kq_tail = 0; mouse_index = 0;
    shift_down = ctrl_down = caps_lock = extended_key = 0;
    /* Keyboard + mouse enabled, controller interrupts off (polling). */
    ps2_command(0xADU);
    ps2_command(0x20U);
    uint8_t command_byte = 0;
    if (ps2_wait_output_ready()) command_byte = inb(0x60);
    command_byte |= 0x40U;           /* Set 1 translation. */
    command_byte &= (uint8_t)~0x03U; /* IRQ1/IRQ12 masked. */
    command_byte &= (uint8_t)~0x30U; /* both port clocks enabled */
    ps2_command(0x60U); ps2_data(command_byte);
    ps2_command(0xAEU);
    ps2_command(0xA8U);              /* enable aux (mouse) port */
    ps2_wait_input_clear(); outb(0x60, 0xF4U);
    if (ps2_wait_output_ready()) (void)inb(0x60);
    keyboard_online = 1;
    if (mouse_command(0xF6U))
    {
        /* IntelliMouse magic sequence: sample rate 200,100,80 then read ID 3. */
        if (mouse_command(0xF3U) && mouse_command(200U) &&
            mouse_command(0xF3U) && mouse_command(100U) &&
            mouse_command(0xF3U) && mouse_command(80U) &&
            mouse_command(0xF2U) && ps2_wait_output_ready())
        {
            if (inb(0x60) == 3U) mouse_has_wheel = 1;
        }
        if (mouse_command(0xF4U)) mouse_online = 1;
    }
    if (mouse_online && mouse_has_wheel)
        uart_puts("Keyboard initialized (PS/2 keyboard + wheel mouse; COM1 serial input).\r\n");
    else
        uart_puts("Keyboard initialized (PS/2 keyboard; no wheel mouse; COM1 serial input).\r\n");
}

int keyboard_try_getc(char* out)
{
    if (!out) return 0;
    ps2_pump();
    if (kq_tail != kq_head)
    {
        *out = (char)kq[kq_tail];
        kq_tail = (kq_tail + 1U) % KQ_SIZE;
        console_snap_to_bottom();
        return 1;
    }
    console_flush(); /* idle: make sure pending output is on screen */
    return 0;
}

int keyboard_has_data()
{
    ps2_pump();
    return kq_tail != kq_head;
}

char keyboard_getc()
{
    char c;
    while (!keyboard_try_getc(&c)) interrupt_idle();
    return c;
}
