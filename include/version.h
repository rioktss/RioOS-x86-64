#ifndef MYKERNEL_VERSION_H
#define MYKERNEL_VERSION_H
#define MYKERNEL_VERSION_MAJOR 1
#define MYKERNEL_VERSION_MINOR 11
#define MYKERNEL_VERSION_PATCH 0
#define MYKERNEL_VERSION_STRING "1.11.0"

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
