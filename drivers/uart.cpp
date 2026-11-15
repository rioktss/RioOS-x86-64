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

#define COM1 0x3F8

static uart_output_hook_t output_hook = 0;
static int output_hook_mirror = 0;
static uart_output_flush_hook_t output_flush_hook = 0;

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

int uart_try_getc() { return (inb(COM1 + 5) & 1U) ? (int)inb(COM1) : -1; }
int uart_has_data() { return (inb(COM1 + 5) & 1U) != 0; }
char uart_getc() { int c; while ((c = uart_try_getc()) < 0) asm volatile("pause"); return (char)c; }

extern "C" void x86_uart_init() { serial_init(); }
