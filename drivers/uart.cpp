/*
 * MIT License
 *
 * Copyright (c) 2026 Rio okta kusuma
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files, to deal in the Software
 * without restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "types.h"
#include "uart.h"
#include "console.h"
#include "x86_io.h"
#include "random.h"

#define COM1 0x3F8

static uart_output_hook_t output_hook = 0;
static int output_hook_mirror = 0;
static uart_output_flush_hook_t output_flush_hook = 0;
static uint8_t rx_ring[256];
static unsigned rx_head = 0, rx_tail = 0;
static const char* probe_name = 0;
static int probe_len = 0, probe_pos = -1, probe_line_start = 1, probe_hit = 0;

static void serial_init()
{
    outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03); outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xC7); outb(COM1 + 4, 0x0B);
}

void uart_set_output_hook(uart_output_hook_t hook, int mirror_to_serial)
{ output_hook = hook; output_hook_mirror = mirror_to_serial != 0; }
void uart_clear_output_hook()
{ output_hook = 0; output_hook_mirror = 0; output_flush_hook = 0; }
void uart_init() { serial_init(); }

void uart_error_probe_begin(const char* name, int len, UartProbeState* save)
{
    if (save) { save->name = probe_name; save->len = probe_len; save->hit = probe_hit; }
    probe_name = name; probe_len = len; probe_pos = -1; probe_hit = 0; probe_line_start = 1;
}
int uart_error_probe_end(const UartProbeState* restore)
{
    int hit = probe_hit;
    if (restore) { probe_name = restore->name; probe_len = restore->len; probe_hit = restore->hit; }
    else { probe_name = 0; probe_len = 0; probe_hit = 0; }
    probe_pos = -1;
    return hit;
}
static void probe_feed(char c)
{
    if (!probe_name) return;
    static const char unk[] = "Unknown command:";
    static int unk_pos = -1;
    if (probe_line_start) { probe_pos = 0; unk_pos = 0; }
    if (probe_pos >= 0) {
        if (probe_pos < probe_len) { if (c == probe_name[probe_pos]) ++probe_pos; else probe_pos = -1; }
        else { if (c == ':' && probe_len > 0) probe_hit = probe_hit ? probe_hit : 1; probe_pos = -1; }
    }
    if (unk_pos >= 0) {
        if (unk[unk_pos] == '\0') { probe_hit = 2; unk_pos = -1; }
        else if (c == unk[unk_pos]) { ++unk_pos; if (unk[unk_pos] == '\0') { probe_hit = 2; unk_pos = -1; } }
        else unk_pos = -1;
    }
    probe_line_start = (c == '\n');
}
int uart_at_line_start(void) { return probe_line_start; }
void uart_poll_job_keys(int (*filter)(int))
{
    for (;;) {
        int c = (inb(COM1 + 5) & 1U) ? (int)inb(COM1) : -1;
        if (c < 0) break;
        random_add_event();
        if (filter && filter(c)) continue;
        unsigned next = (rx_head + 1U) & 255U;
        if (next == rx_tail) break;
        rx_ring[rx_head] = (uint8_t)c; rx_head = next;
    }
}
int uart_output_hook_active() { return output_hook != 0; }
void uart_set_output_flush_hook(uart_output_flush_hook_t hook) { output_flush_hook = hook; }
void uart_flush_output()
{
    if (output_flush_hook) output_flush_hook();
    console_flush();
}

void uart_putc_serial_only(char c)
{
    while ((inb(COM1 + 5) & 0x20U) == 0) { }
    outb(COM1, (uint8_t)c);
}

void uart_puts_serial_only(const char* s)
{
    if (!s) return;
    while (*s) uart_putc_serial_only(*s++);
}

void uart_putc(char c)
{
    probe_feed(c);
    if (output_hook) { output_hook(c); if (!output_hook_mirror) return; }
    /* Everything printed to serial is mirrored to the QEMU window terminal
       once it is active. */
    if (console_active()) console_putc(c);
    uart_putc_serial_only(c);
}

void uart_puts(const char* s)
{
    if (!s) return;
    while (*s) uart_putc(*s++);
}

int uart_try_getc()
{
    if (rx_tail != rx_head) { int c = rx_ring[rx_tail]; rx_tail = (rx_tail + 1U) & 255U; return c; }
    if ((inb(COM1 + 5) & 1U) == 0) return -1;
    random_add_event();
    return (int)inb(COM1);
}
int uart_has_data() { return rx_head != rx_tail || (inb(COM1 + 5) & 1U) != 0; }
char uart_getc() { int c; while ((c = uart_try_getc()) < 0) asm volatile("pause"); return (char)c; }

extern "C" void x86_uart_init() { serial_init(); }
