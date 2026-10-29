#include "fb.h"
#ifndef HOST_TEST
#include "uart.h"
#include "x86_io.h"
#endif

#define FB_DEFAULT_WIDTH  1024
#define FB_DEFAULT_HEIGHT 768
#define FB_BACKGROUND     0x00000000U

static uint32_t* fb_base = 0;
static int fb_w = FB_DEFAULT_WIDTH;
static int fb_h = FB_DEFAULT_HEIGHT;
static int fb_ready = 0;
static int fb_display_ready = 0;

#ifndef HOST_TEST
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define VBE_INDEX          0x01CE
#define VBE_DATA           0x01CF

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) | ((uint32_t)function << 8) | (offset & 0xFCU);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static uint64_t find_qemu_vga_lfb()
{
    for (uint8_t slot = 0; slot < 32; ++slot)
    {
        uint32_t id = pci_read32(0, slot, 0, 0x00);
        if ((id & 0xFFFFU) == 0xFFFFU) continue;
        uint32_t class_rev = pci_read32(0, slot, 0, 0x08);
        uint8_t class_code = (uint8_t)(class_rev >> 24);
        uint8_t subclass = (uint8_t)(class_rev >> 16);
        if (class_code != 0x03U && !(class_code == 0x00U && subclass == 0x01U)) continue;
        uint32_t bar0 = pci_read32(0, slot, 0, 0x10);
        if ((bar0 & 1U) != 0U) continue; /* VGA LFB must be a memory BAR. */
        uint64_t base = (uint64_t)(bar0 & 0xFFFFFFF0U);
        if (base >= 0x10000000ULL && base + 4ULL * FB_DEFAULT_WIDTH * FB_DEFAULT_HEIGHT < 0x100000000ULL)
            return base;
    }
    return 0;
}

static void vbe_set(uint16_t index, uint16_t value)
{
    outw(VBE_INDEX, index);
    outw(VBE_DATA, value);
}

static uint16_t vbe_get(uint16_t index)
{
    outw(VBE_INDEX, index);
    return inw(VBE_DATA);
}

static int x86_framebuffer_init()
{
    uint64_t address = find_qemu_vga_lfb();
    if (!address) return -1;
    uint16_t id = vbe_get(0);
    if (id < 0xB0C0U) return -1;

    /* Bochs/QEMU VBE: direct 32-bit XRGB framebuffer, no BIOS calls needed. */
    vbe_set(4, 0);
    vbe_set(1, FB_DEFAULT_WIDTH);
    vbe_set(2, FB_DEFAULT_HEIGHT);
    vbe_set(3, 32);
    vbe_set(6, FB_DEFAULT_WIDTH);
    vbe_set(8, 0);
    vbe_set(9, 0);
    vbe_set(4, 0x41U); /* enabled + linear framebuffer */
    if (vbe_get(1) != FB_DEFAULT_WIDTH || vbe_get(2) != FB_DEFAULT_HEIGHT || vbe_get(3) != 32)
        return -1;

    fb_base = (uint32_t*)address;
    fb_w = FB_DEFAULT_WIDTH;
    fb_h = FB_DEFAULT_HEIGHT;
    fb_ready = 1;
    fb_display_ready = 1;
    return 0;
}
#endif

void fb_fill_rect(int x, int y, int width, int height, uint32_t color)
{
    if (!fb_ready || fb_base == 0 || width <= 0 || height <= 0) return;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x >= fb_w || y >= fb_h) return;
    if (width > fb_w - x) width = fb_w - x;
    if (height > fb_h - y) height = fb_h - y;
    if (width <= 0 || height <= 0) return;

    for (int row = 0; row < height; ++row)
    {
        uint32_t* dst = fb_base + (uint64_t)(y + row) * (uint64_t)fb_w + (uint64_t)x;
        for (int col = 0; col < width; ++col) dst[col] = color;
    }
}

void fb_draw_pixel(int x, int y, uint32_t color)
{
    if (!fb_ready || fb_base == 0 || x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    fb_base[(uint64_t)y * (uint64_t)fb_w + (uint64_t)x] = color;
}

void fb_init()
{
    fb_base = 0;
    fb_w = FB_DEFAULT_WIDTH;
    fb_h = FB_DEFAULT_HEIGHT;
    fb_ready = 0;
    fb_display_ready = 0;
#ifndef HOST_TEST
    if (x86_framebuffer_init() == 0)
    {
        fb_clear(FB_BACKGROUND);
        uart_puts("Framebuffer: QEMU/Bochs VBE 1024x768 XRGB8888 is ready.\r\n");
    }
    else
    {
        uart_puts("Framebuffer: no QEMU VGA VBE linear framebuffer; serial console only.\r\n");
    }
#endif
}

void fb_clear(uint32_t color)
{
    if (!fb_ready || fb_base == 0) return;
    fb_fill_rect(0, 0, fb_w, fb_h, color);
}

void fb_present_rows(int y0, int y1)
{
    /* The VBE linear framebuffer is scanned out directly: nothing to flush. */
    (void)y0; (void)y1;
}

void fb_present()
{
    fb_present_rows(0, fb_h);
}

uint32_t* fb_buffer() { return fb_ready ? fb_base : 0; }
int fb_stride_pixels() { return fb_w; }
int fb_get_width() { return fb_w; }
int fb_get_height() { return fb_h; }
int fb_is_ready() { return fb_ready && fb_base != 0; }
int fb_has_display() { return fb_display_ready; }

#ifdef HOST_TEST
int fb_test_attach(uint32_t* pixels, int width, int height)
{
    if (pixels == 0 || width < 16 || height < 16 || width > 4096 || height > 4096)
    {
        fb_base = 0; fb_ready = 0; fb_display_ready = 0; return -1;
    }
    fb_base = pixels; fb_w = width; fb_h = height;
    fb_ready = 1; fb_display_ready = 1;
    fb_clear(FB_BACKGROUND);
    return 0;
}
uint32_t fb_test_pixel(int x, int y)
{
    if (!fb_ready || fb_base == 0 || x < 0 || y < 0 || x >= fb_w || y >= fb_h) return 0;
    return fb_base[(uint64_t)y * (uint64_t)fb_w + (uint64_t)x];
}
#endif
