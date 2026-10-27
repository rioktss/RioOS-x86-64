#include "user_api.h"
#include "types.h"

/*
 * RioOS nano v1.2 - tiny ring-3 text editor (x86-64 port; behaviour identical
 * to the AArch64 EL0 version).
 *
 * File paths are resolved by the kernel relative to the shell's current
 * directory, so `nano test.txt` saves where the shell currently is.
 *
 * Keys: printable = insert at end, Enter = newline, Backspace = delete,
 *       CTRL+S or CTRL+O = save, CTRL+X = exit.
 */

#define BUF_MAX 4096

static char buffer[BUF_MAX];
static uint64_t len = 0;
static char fname[128];
static const char default_name[] = "untitled.txt";

static const char msg_title[]    = "\033[2J\033[H=== RioOS nano v1.2 ===\r\nFile: ";
static const char msg_keys[]     = " | CTRL+S save | CTRL+X exit\r\n------------------------\r\n";
static const char msg_big[]      = "nano: file too large (max 4095 bytes), not opened\r\n";
static const char msg_read_fail[]= "nano: cannot read file; refusing to edit an unreadable file\r\n";
static const char msg_noname[]   = "nano: usage: nano <file>\r\n";
static const char msg_saved[]    = "\r\n[Saved!]\r\n";
static const char msg_fail[]     = "\r\n[Save failed; text remains in memory]\r\n";
static const char msg_denied[]   = "\r\n[Permission denied; try /home/user]\r\n";
static const char msg_no_space[] = "\r\n[Save failed: disk is full]\r\n";
static const char msg_storage[]  = "\r\n[Save failed: storage I/O error]\r\n";
static const char msg_metadata[] = "\r\n[Save failed: filesystem metadata commit error]\r\n";
static const char msg_exit[]     = "\r\n[Exit]\r\n";
static const char msg_bs[]       = "\b \b";

static void sys_puts(const char* s) { (void)user_syscall1(USER_SYS_WRITE, (uint64_t)s); }

static void put_char(char c) { (void)user_write(&c, 1); }

static void put_char_nl(char c)
{
    if (c == 10) put_char(13);
    put_char(c);
}

static int get_char()
{
    char c;
    for (;;)
    {
        if (user_read(&c, 1) == 1) return (unsigned char)c;
        asm volatile("pause");
    }
}

static void redraw()
{
    sys_puts(msg_title);
    sys_puts(fname);
    sys_puts(msg_keys);
    for (uint64_t i = 0; i < len; ++i) put_char_nl(buffer[i]);
}

static int nano_main()
{
    int64_t n = (int64_t)user_syscall2(USER_SYS_GETARG, (uint64_t)fname, 128);
    if (n < 0) { sys_puts(msg_noname); return 1; }
    if (n == 0)
    {
        int i = 0;
        for (; i < 16; ++i)
        {
            fname[i] = default_name[i];
            if (!default_name[i]) break;
        }
        if (i == 16) { sys_puts(msg_noname); return 1; }
    }

    int64_t rd = (int64_t)user_syscall3(USER_SYS_FILE_READ, (uint64_t)fname, (uint64_t)buffer, 4095);
    if (rd >= 0) len = (uint64_t)rd;
    else if (rd == -27) { sys_puts(msg_big); return 1; }
    else if (rd == -2) len = 0;
    else { sys_puts(msg_read_fail); return 1; }

    redraw();
    for (;;)
    {
        int c = get_char();
        if (c == 15 || c == 19)
        {
            int64_t w = (int64_t)user_syscall3(USER_SYS_FILE_WRITE, (uint64_t)fname, (uint64_t)buffer, len);
            if (w >= 0 && (uint64_t)w == len) { sys_puts(msg_saved); continue; }
            if (w >= 0) { sys_puts(msg_fail); continue; }
            switch (w)
            {
                case -13: sys_puts(msg_denied); break;
                case -3:  sys_puts(msg_no_space); break;
                case -4:  sys_puts(msg_storage); break;
                case -5: case -6: case -7: sys_puts(msg_metadata); break;
                default:  sys_puts(msg_fail); break;
            }
            continue;
        }
        if (c == 24) { sys_puts(msg_exit); return 0; }
        if (c == 8 || c == 127)
        {
            if (len == 0) continue;
            --len;
            if (buffer[len] == 10) redraw();
            else sys_puts(msg_bs);
            continue;
        }
        char ch;
        if (c == 13 || c == 10) ch = 10;
        else if (c >= 32 && c <= 126) ch = (char)c;
        else continue;
        if (len >= 4095) continue;
        buffer[len++] = ch;
        put_char_nl(ch);
    }
}

extern "C" void _start()
{
    int rc = nano_main();
    (void)user_syscall1(USER_SYS_EXIT, (uint64_t)rc);
    for (;;) asm volatile("pause");
}
