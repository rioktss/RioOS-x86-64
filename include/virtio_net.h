#ifndef MYKERNEL_X86_VIRTIO_NET_COMPAT_H
#define MYKERNEL_X86_VIRTIO_NET_COMPAT_H
/* x86-64 compatibility surface for the shared RioOS command layer.
 * The x86 transport is legacy VirtIO-PCI over I/O ports, not ARM VirtIO-MMIO. */
#include "net_device.h"
static inline void virtio_net_init(void) { net_device_init(); }
static inline int virtio_net_present(void) { return net_device_present(); }
static inline int virtio_net_ready(void) { return net_device_ready(); }
static inline void virtio_net_get_mac(uint8_t mac[6]) { net_device_get_mac(mac); }
static inline int virtio_net_send(const void* frame, uint32_t length) { return net_device_send(frame, length); }
static inline int virtio_net_receive(void* frame, uint32_t capacity) { return net_device_receive(frame, capacity); }
static inline uint64_t virtio_net_base(void) { return net_device_base(); }
static inline int virtio_net_irq(void) { return net_device_irq(); }
static inline void virtio_net_status(void) { net_device_status(); }
#endif
