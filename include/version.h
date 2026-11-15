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

#ifndef MYKERNEL_VERSION_H
#define MYKERNEL_VERSION_H
#define MYKERNEL_VERSION_MAJOR 1
#define MYKERNEL_VERSION_MINOR 13
#define MYKERNEL_VERSION_PATCH 0
#define MYKERNEL_VERSION_STRING "1.13.0"

#ifdef MYKERNEL_X86_64
#define MYKERNEL_ARCH_NAME "x86-64"
#else
#define MYKERNEL_ARCH_NAME "ARM64"
#endif

#ifdef MYKERNEL_X86_64
#define MYKERNEL_BLOCK_NAME "ata-pio"
#else
#define MYKERNEL_BLOCK_NAME "virtio-blk"
#endif

#ifdef MYKERNEL_X86_64
#define MYKERNEL_ARCH_UPPER "X86-64"
#else
#define MYKERNEL_ARCH_UPPER "ARM64"
#endif

void command_version();
#endif
