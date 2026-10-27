#include "net_device.h"
#include "uart.h"
#include "timer.h"

/* Legacy (transitional) virtio-net over PCI I/O space, polled. QEMU's default
   `-device virtio-net-pci` exposes this interface (vendor 1AF4, device 1000). */
#define VIO_DEVICE_FEATURES 0x00
#define VIO_GUEST_FEATURES  0x04
#define VIO_QUEUE_PFN       0x08
#define VIO_QUEUE_SIZE      0x0C
#define VIO_QUEUE_SELECT    0x0E
#define VIO_QUEUE_NOTIFY    0x10
#define VIO_STATUS          0x12
#define VIO_ISR             0x13
#define VIO_CONFIG          0x14

#define ST_ACK     0x01U
#define ST_DRIVER  0x02U
#define ST_OK      0x04U
#define ST_FAILED  0x80U
#define F_MAC      (1U << 5)
#define DESC_WRITE 2U

#define QMAX        256U           /* upper bound of the device's queue size */
#define RX_BUFS     8U
#define NET_HDR     10U            /* no MRG_RXBUF negotiated */
#define BUF_SIZE    2048U
#define TIMEOUT_MS  2000ULL

struct VqDesc { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
/* Legacy ring layout: desc[N], avail, pad to 4096, used. Sized for QMAX. */
struct Vq {
    VqDesc desc[QMAX];
    uint16_t avail_flags, avail_idx, avail_ring[QMAX], used_event;
    uint8_t pad[4096 - ((sizeof(VqDesc) * QMAX + 6 + 2 * QMAX) % 4096)];
    uint16_t used_flags, used_idx;
    struct { uint32_t id, len; } used_ring[QMAX];
    uint16_t avail_event;
} __attribute__((aligned(4096)));

static_assert(__builtin_offsetof(Vq, used_flags) == 8192, "legacy used ring must start at page boundary");
static Vq rxq, txq;
static uint8_t rx_buf[RX_BUFS][BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buf[BUF_SIZE] __attribute__((aligned(16)));

static uint16_t io_base = 0;
static int present = 0, ready = 0;
static uint8_t mac_address[6];
static uint16_t rx_last = 0, tx_last = 0;
static uint32_t rx_qsize = 0, tx_qsize = 0;

static inline void outb(uint16_t p, uint8_t v) { asm volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { asm volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline void outl(uint16_t p, uint32_t v) { asm volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; asm volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; asm volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint32_t inl(uint16_t p) { uint32_t v; asm volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void barrier() { asm volatile("" ::: "memory"); }

static uint32_t pci_cfg_read(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off)
{
    outl(0xCF8, 0x80000000U | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFCU));
    return inl(0xCFC);
}
static void pci_cfg_write(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off, uint32_t v)
{
    outl(0xCF8, 0x80000000U | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFCU));
    outl(0xCFC, v);
}

static void queue_init(Vq* q, uint16_t index, uint32_t* size_out)
{
    outw(io_base + VIO_QUEUE_SELECT, index);
    uint32_t n = inw(io_base + VIO_QUEUE_SIZE);
    *size_out = n;
    uint8_t* p = (uint8_t*)q;
    for (uint64_t i = 0; i < sizeof(Vq); ++i) p[i] = 0;
    outl(io_base + VIO_QUEUE_PFN, (uint32_t)((uint64_t)q >> 12));
}

void net_device_init()
{
    present = ready = 0; io_base = 0; rx_last = tx_last = 0;
    for (int i = 0; i < 6; ++i) mac_address[i] = 0;

    int found = 0; uint32_t fb = 0, fd = 0;
    for (uint32_t bus = 0; bus < 8 && !found; ++bus)
        for (uint32_t dev = 0; dev < 32 && !found; ++dev)
        {
            uint32_t id = pci_cfg_read(bus, dev, 0, 0);
            if ((id & 0xFFFFU) == 0x1AF4U && (id >> 16) == 0x1000U) { found = 1; fb = bus; fd = dev; }
        }
    if (!found) { uart_puts("Network: no virtio-net PCI device found.\r\n"); return; }

    uint32_t bar0 = pci_cfg_read(fb, fd, 0, 0x10);
    if (!(bar0 & 1U)) { uart_puts("Network: virtio-net BAR0 is not an I/O BAR.\r\n"); return; }
    io_base = (uint16_t)(bar0 & 0xFFFCU);
    uint32_t cmd = pci_cfg_read(fb, fd, 0, 0x04);
    pci_cfg_write(fb, fd, 0, 0x04, (cmd & 0xFFFFU) | 0x5U); /* I/O space + bus master */
    present = 1;

    outb(io_base + VIO_STATUS, 0);
    outb(io_base + VIO_STATUS, ST_ACK);
    outb(io_base + VIO_STATUS, ST_ACK | ST_DRIVER);
    uint32_t features = inl(io_base + VIO_DEVICE_FEATURES);
    if (!(features & F_MAC)) { uart_puts("Network: virtio-net lacks MAC feature.\r\n"); outb(io_base + VIO_STATUS, ST_FAILED); return; }
    outl(io_base + VIO_GUEST_FEATURES, F_MAC);
    for (int i = 0; i < 6; ++i) mac_address[i] = inb(io_base + VIO_CONFIG + i);

    queue_init(&rxq, 0, &rx_qsize);
    queue_init(&txq, 1, &tx_qsize);
    if (rx_qsize == 0 || rx_qsize > QMAX || tx_qsize == 0 || tx_qsize > QMAX)
    {
        uart_puts("Network: virtio-net queue size unsupported.\r\n");
        outb(io_base + VIO_STATUS, ST_FAILED);
        return;
    }
    for (uint32_t i = 0; i < RX_BUFS && i < rx_qsize; ++i)
    {
        rxq.desc[i].addr = (uint64_t)&rx_buf[i][0];
        rxq.desc[i].len = BUF_SIZE;
        rxq.desc[i].flags = DESC_WRITE;
        rxq.desc[i].next = 0;
        rxq.avail_ring[i] = (uint16_t)i;
    }
    barrier();
    rxq.avail_idx = (uint16_t)(RX_BUFS < rx_qsize ? RX_BUFS : rx_qsize);
    barrier();
    outb(io_base + VIO_STATUS, ST_ACK | ST_DRIVER | ST_OK);
    outw(io_base + VIO_QUEUE_NOTIFY, 0);
    ready = 1;
    uart_puts("Network: virtio-net (PCI legacy) ready.\r\n");
}

int net_device_present() { return present; }
int net_device_ready() { return ready; }
void net_device_get_mac(uint8_t mac[6]) { if (mac) for (int i = 0; i < 6; ++i) mac[i] = mac_address[i]; }
uint64_t net_device_base() { return io_base; }
int net_device_irq() { return -1; }

int net_device_send(const void* frame, uint32_t length)
{
    if (!ready || !frame || length == 0 || length > BUF_SIZE - NET_HDR) return -22;
    for (uint32_t i = 0; i < NET_HDR; ++i) tx_buf[i] = 0;
    const uint8_t* src = (const uint8_t*)frame;
    for (uint32_t i = 0; i < length; ++i) tx_buf[NET_HDR + i] = src[i];
    txq.desc[0].addr = (uint64_t)tx_buf;
    txq.desc[0].len = NET_HDR + length;
    txq.desc[0].flags = 0;
    txq.desc[0].next = 0;
    uint16_t idx = txq.avail_idx;
    txq.avail_ring[idx % tx_qsize] = 0;
    barrier();
    txq.avail_idx = (uint16_t)(idx + 1U);
    barrier();
    outw(io_base + VIO_QUEUE_NOTIFY, 1);
    uint64_t start = timer_millis();
    while (*(volatile uint16_t*)&txq.used_idx == tx_last)
    {
        if (timer_millis() - start >= TIMEOUT_MS) return -110;
        asm volatile("pause");
    }
    tx_last = (uint16_t)(tx_last + 1U);
    return 0;
}

static void recycle(uint32_t id)
{
    rxq.avail_ring[rxq.avail_idx % rx_qsize] = (uint16_t)id;
    barrier();
    rxq.avail_idx = (uint16_t)(rxq.avail_idx + 1U);
    barrier();
    outw(io_base + VIO_QUEUE_NOTIFY, 0);
}

int net_device_receive(void* frame, uint32_t capacity)
{
    if (!ready || !frame || capacity == 0) return -22;
    if (*(volatile uint16_t*)&rxq.used_idx == rx_last) return 0;
    uint32_t slot = rx_last % rx_qsize;
    uint32_t id = rxq.used_ring[slot].id, len = rxq.used_ring[slot].len;
    rx_last = (uint16_t)(rx_last + 1U);
    if (id >= RX_BUFS) return -5;
    if (len < NET_HDR || len > BUF_SIZE) { recycle(id); return -5; }
    uint32_t flen = len - NET_HDR;
    if (flen > capacity) { recycle(id); return -90; }
    const uint8_t* src = &rx_buf[id][NET_HDR];
    uint8_t* dst = (uint8_t*)frame;
    for (uint32_t i = 0; i < flen; ++i) dst[i] = src[i];
    recycle(id);
    return (int)flen;
}

static void hex8(uint8_t v) { static const char h[] = "0123456789ABCDEF"; uart_putc(h[v >> 4]); uart_putc(h[v & 15]); }
void net_device_status()
{
    uart_puts("Network device status\r\n---------------------\r\nPresent : ");
    uart_puts(present ? "yes (virtio-net PCI)\r\n" : "no\r\n");
    uart_puts("Ready   : "); uart_puts(ready ? "yes\r\n" : "no\r\n");
    uart_puts("MAC     : ");
    for (int i = 0; i < 6; ++i) { hex8(mac_address[i]); if (i != 5) uart_putc(':'); }
    uart_puts("\r\nIRQ     : polled\r\n");
}
