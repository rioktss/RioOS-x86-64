#include "storage.h"
#include "uart.h"
#include "x86_io.h"

/* -------------------------- ATA PIO primary master ----------------------- */
#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECCOUNT0  0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_HDDEVSEL   0x1F6
#define ATA_COMMAND    0x1F7
#define ATA_STATUS     0x1F7
#define ATA_ALTSTATUS  0x3F6
#define ATA_CONTROL    0x3F6
#define ATA_SR_BSY     0x80
#define ATA_SR_DRDY    0x40
#define ATA_SR_DF      0x20
#define ATA_SR_DRQ     0x08
#define ATA_SR_ERR     0x01
static int ata_online = 0;
static uint64_t ata_sectors = 0;

static void ata_delay_400ns()
{
    (void)inb(ATA_ALTSTATUS); (void)inb(ATA_ALTSTATUS);
    (void)inb(ATA_ALTSTATUS); (void)inb(ATA_ALTSTATUS);
}

static int ata_wait_ready(int require_drq)
{
    for (uint32_t i = 0; i < 2000000U; ++i)
    {
        uint8_t status = inb(ATA_STATUS);
        if (status == 0xFFU || status == 0U) return -1;
        if ((status & ATA_SR_BSY) != 0U) continue;
        if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0U) return -1;
        if (!require_drq || (status & ATA_SR_DRQ) != 0U) return 0;
    }
    return -1;
}

static int ata_select(uint64_t sector)
{
    if (sector >= 0x10000000ULL) return -1; /* LBA28 */
    outb(ATA_HDDEVSEL, (uint8_t)(0xE0U | ((sector >> 24) & 0x0FU)));
    ata_delay_400ns();
    outb(ATA_SECCOUNT0, 1);
    outb(ATA_LBA0, (uint8_t)sector);
    outb(ATA_LBA1, (uint8_t)(sector >> 8));
    outb(ATA_LBA2, (uint8_t)(sector >> 16));
    return 0;
}

void storage_init()
{
    ata_online = 0;
    ata_sectors = 0;
    outb(ATA_CONTROL, 0x02U); /* Disable ATA device IRQs; all I/O is polled. */
    outb(ATA_HDDEVSEL, 0xA0U);
    ata_delay_400ns();
    outb(ATA_SECCOUNT0, 0);
    outb(ATA_LBA0, 0); outb(ATA_LBA1, 0); outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, 0xECU); /* IDENTIFY DEVICE */
    uint8_t status = inb(ATA_STATUS);
    if (status == 0U || status == 0xFFU)
    {
        uart_puts("Storage: no ATA primary-master disk detected; using RAM filesystem.\r\n");
        return;
    }
    if (ata_wait_ready(1) != 0)
    {
        uart_puts("Storage: ATA IDENTIFY failed; using RAM filesystem.\r\n");
        return;
    }
    uint16_t identify[256];
    for (int i = 0; i < 256; ++i) identify[i] = inw(ATA_DATA);
    uint64_t sectors28 = (uint64_t)identify[60] | ((uint64_t)identify[61] << 16);
    uint64_t sectors48 = (uint64_t)identify[100] | ((uint64_t)identify[101] << 16) |
                         ((uint64_t)identify[102] << 32) | ((uint64_t)identify[103] << 48);
    ata_sectors = sectors48 ? sectors48 : sectors28;
    if (ata_sectors == 0) ata_sectors = sectors28;
    ata_online = ata_sectors != 0;
    if (ata_online)
    {
        uart_puts("Storage: ATA PIO primary master ready; sectors = ");
        char text[24]; int n = 0; uint64_t value = ata_sectors;
        do { text[n++] = (char)('0' + value % 10U); value /= 10U; } while (value && n < 23);
        while (n) uart_putc(text[--n]);
        uart_puts(".\r\n");
    }
    else uart_puts("Storage: ATA disk reported no addressable sectors; RAM filesystem selected.\r\n");
}

int storage_ready() { return ata_online; }
int storage_read_sector(uint64_t sector, void* buffer)
{
    if (!ata_online || !buffer || sector >= ata_sectors || ata_select(sector) != 0) return -1;
    outb(ATA_COMMAND, 0x20U);
    if (ata_wait_ready(1) != 0) return -1;
    uint16_t* out = (uint16_t*)buffer;
    for (int i = 0; i < 256; ++i) out[i] = inw(ATA_DATA);
    return 0;
}
int storage_write_sector(uint64_t sector, const void* buffer)
{
    if (!ata_online || !buffer || sector >= ata_sectors || ata_select(sector) != 0) return -1;
    outb(ATA_COMMAND, 0x30U);
    if (ata_wait_ready(1) != 0) return -1;
    const uint16_t* in = (const uint16_t*)buffer;
    for (int i = 0; i < 256; ++i) outw(ATA_DATA, in[i]);
    ata_delay_400ns();
    return ata_wait_ready(0);
}
int storage_flush()
{
    if (!ata_online) return -1;
    outb(ATA_COMMAND, 0xE7U);
    return ata_wait_ready(0);
}
void storage_status()
{
    if (ata_online) uart_puts("Storage: ATA PIO primary master (LBA28/LBA48 identify, polling).\r\n");
    else uart_puts("Storage offline; filesystem uses RAM fallback.\r\n");
}
