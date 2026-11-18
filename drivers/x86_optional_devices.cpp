#include "virtio_snd.h"
#include "usb.h"
#include "pci.h"
#include "nvme.h"
#include "ahci.h"
#include "ninep.h"
#include "virtio_gpu.h"
#include "uart.h"

/* These transports are not silently emulated: the x86-64 image currently
 * uses ATA PIO, PS/2, legacy PCI VirtIO-Net and the VBE framebuffer. */
int snd_init() { return 0; }
int snd_present() { return 0; }
const SndInfo* snd_get_info() { return 0; }
int snd_play_pcm16(const void*, uint32_t, uint32_t, uint32_t) { return SND_E_NODEV; }
int snd_play_tone(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) { return SND_E_NODEV; }
int snd_play_wav(const uint8_t*, uint32_t, uint32_t*, uint32_t*, uint32_t*) { return SND_E_NODEV; }
uint64_t snd_bytes_played() { return 0; }
uint32_t snd_error_count() { return 0; }
const char* snd_error_text(int rc) { return rc == SND_E_NODEV ? "no x86 sound transport" : "sound unavailable"; }

void usb_init() {}
int usb_present() { return 0; }
int usb_keyboard_count() { return 0; }
int usb_mouse_count() { return 0; }
int usb_tablet_count() { return 0; }
int usb_hub_count() { return 0; }
void usb_poll() {}
void usb_rescan() {}
int usb_try_get_key(char*) { return 0; }
int usb_poll_event(RioInputEvent*) { return 0; }
void usb_status() {}
int usb_selftest() { return -1; }

void pci_init() {}
int pci_present() { return 0; }
int pci_device_count() { return 0; }
const PciDevice* pci_device(int) { return 0; }
const PciDevice* pci_find_class(uint8_t, uint8_t, uint8_t, int) { return 0; }
const char* pci_class_name(uint8_t, uint8_t) { return "unknown"; }
void pci_list() { uart_puts("PCI enumeration/BAR discovery is not yet implemented in this x86-64 port.\r\n"); }

void nvme_init() {}
int nvme_ready() { return 0; }
uint64_t nvme_capacity() { return 0; }
int nvme_read(uint64_t, void*) { return -1; }
int nvme_write(uint64_t, const void*) { return -1; }
int nvme_flush() { return -1; }
void nvme_status() {}
int nvme_selftest(int) { return -1; }
void ahci_init() {}
int ahci_ready() { return 0; }
uint64_t ahci_capacity() { return 0; }
int ahci_read(uint64_t, void*) { return -1; }
int ahci_write(uint64_t, const void*) { return -1; }
int ahci_flush() { return -1; }
void ahci_status() {}
int ahci_selftest(int) { return -1; }

int ninep_init() { return 0; }
int ninep_share_count() { return 0; }
const char* ninep_share_tag(int) { return 0; }
int ninep_share_find(const char*) { return -1; }
uint32_t ninep_share_msize(int) { return 0; }
int ninep_share_alive(int) { return 0; }
const NinepOps* ninep_ops() { return 0; }
uint32_t ninep_request_count() { return 0; }

int virtio_gpu_init(void*, uint32_t, uint32_t) { return 0; }
int virtio_gpu_ready() { return 0; }
void virtio_gpu_flush_rows(int, int) {}
void virtio_gpu_display(uint32_t* w, uint32_t* h, int* e) { if (w) *w = 0; if (h) *h = 0; if (e) *e = 0; }
uint32_t virtio_gpu_flush_count() { return 0; }

/* No VirtIO RNG PCI transport is wired into this legacy x86 port yet. */
int virtio_rng_init() { return 0; }
int virtio_rng_read(uint8_t*, uint32_t) { return 0; }
