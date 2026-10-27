#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "types.h"

void net_device_init();
int net_device_present();
int net_device_ready();
void net_device_get_mac(uint8_t mac[6]);
int net_device_send(const void* frame, uint32_t length);
int net_device_receive(void* frame, uint32_t capacity);
uint64_t net_device_base();
int net_device_irq();
void net_device_status();

#endif
