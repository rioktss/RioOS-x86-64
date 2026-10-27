#include "fb.h"
#include "font.h"
#include "ui_font_data.h"
#ifndef HOST_TEST
#include "uart.h"
#endif

#define FB_DEFAULT_WIDTH  1024
#define FB_DEFAULT_HEIGHT 768
#define FB_FONT_WIDTH     8
#define FB_FONT_HEIGHT    16
#define FB_BACKGROUND     0x00000000U

static uint32_t* fb_base = 0;
static int fb_w = FB_DEFAULT_WIDTH;
static int fb_h = FB_DEFAULT_HEIGHT;
static int cur_x = 0;
static int cur_y = 0;
static int fb_ready = 0;
static int fb_display_ready = 0;

#ifndef HOST_TEST
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define VBE_INDEX          0x01CE
#define VBE_DATA           0x01CF

static inline void x86_outw(uint16_t port, uint16_t value)
{ asm volatile("outw %0, %1" :: "a"(value), "Nd"(port)); }
static inline uint16_t x86_inw(uint16_t port)
{ uint16_t value; asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static inline void x86_outl(uint16_t port, uint32_t value)
{ asm volatile("outl %0, %1" :: "a"(value), "Nd"(port)); }
static inline uint32_t x86_inl(uint16_t port)
{ uint32_t value; asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port)); return value; }

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) | ((uint32_t)function << 8) | (offset & 0xFCU);
    x86_outl(PCI_CONFIG_ADDRESS, address);
    return x86_inl(PCI_CONFIG_DATA);
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
    x86_outw(VBE_INDEX, index);
    x86_outw(VBE_DATA, value);
}

static uint16_t vbe_get(uint16_t index)
{
    x86_outw(VBE_INDEX, index);
    return x86_inw(VBE_DATA);
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

static void fb_scroll_one_line()
{
    if (!fb_ready || fb_base == 0 || fb_h <= FB_FONT_HEIGHT) return;
    const uint64_t row_pixels = (uint64_t)fb_w;
    const int remaining_rows = fb_h - FB_FONT_HEIGHT;
    for (int y = 0; y < remaining_rows; ++y)
    {
        uint32_t* dst = fb_base + (uint64_t)y * row_pixels;
        const uint32_t* src = fb_base + (uint64_t)(y + FB_FONT_HEIGHT) * row_pixels;
        for (int x = 0; x < fb_w; ++x) dst[x] = src[x];
    }
    fb_fill_rect(0, fb_h - FB_FONT_HEIGHT, fb_w, FB_FONT_HEIGHT, FB_BACKGROUND);
    cur_y = fb_h - FB_FONT_HEIGHT;
    if (cur_y < 0) cur_y = 0;
}

static void fb_advance_line()
{
    cur_x = 0;
    cur_y += FB_FONT_HEIGHT;
    if (cur_y + FB_FONT_HEIGHT > fb_h) fb_scroll_one_line();
    if (cur_y + FB_FONT_HEIGHT > fb_h)
        cur_y = fb_h >= FB_FONT_HEIGHT ? fb_h - FB_FONT_HEIGHT : 0;
}

static void fb_draw_char(int x, int y, char c, uint32_t color)
{
    if (!fb_ready || fb_base == 0) return;
    unsigned int uc = (unsigned char)c;
    if (uc >= 128U) uc = (unsigned int)'?';
    for (int row = 0; row < FB_FONT_HEIGHT; ++row)
    {
        uint8_t bits = font8x8[uc][row >> 1];
        for (int col = 0; col < FB_FONT_WIDTH; ++col)
        {
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= fb_w || py < 0 || py >= fb_h) continue;
            uint32_t pixel = (bits & (uint8_t)(0x80U >> col)) != 0U ? color : FB_BACKGROUND;
            fb_base[(uint64_t)py * (uint64_t)fb_w + (uint64_t)px] = pixel;
        }
    }
}

void fb_init()
{
#ifdef HOST_TEST
    fb_base = 0;
    fb_ready = 0;
    fb_display_ready = 0;
    cur_x = 0;
    cur_y = 0;
#else
    fb_base = 0;
    fb_w = FB_DEFAULT_WIDTH;
    fb_h = FB_DEFAULT_HEIGHT;
    cur_x = 0;
    cur_y = 0;
    fb_ready = 0;
    fb_display_ready = 0;
    if (x86_framebuffer_init() == 0)
    {
        fb_clear(FB_BACKGROUND);
        uart_puts("Framebuffer: QEMU/Bochs VBE 1024x768 XRGB8888 is ready.\r\n");
    }
    else
    {
        uart_puts("Framebuffer: no supported QEMU VGA VBE linear framebuffer; serial shell remains active.\r\n");
    }
#endif
}

void fb_clear(uint32_t color)
{
    if (!fb_ready || fb_base == 0)
    {
        cur_x = 0;
        cur_y = 0;
        return;
    }
    fb_fill_rect(0, 0, fb_w, fb_h, color);
    cur_x = 0;
    cur_y = 0;
}

void fb_set_cursor(int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    cur_x = x - (x % FB_FONT_WIDTH);
    cur_y = y - (y % FB_FONT_HEIGHT);
    int max_x = fb_w >= FB_FONT_WIDTH ? fb_w - FB_FONT_WIDTH : 0;
    int max_y = fb_h >= FB_FONT_HEIGHT ? fb_h - FB_FONT_HEIGHT : 0;
    if (cur_x > max_x) cur_x = max_x - (max_x % FB_FONT_WIDTH);
    if (cur_y > max_y) cur_y = max_y - (max_y % FB_FONT_HEIGHT);
    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
}

void fb_putc(char c, uint32_t color)
{
    if (!fb_ready || fb_base == 0) return;
    if (c == '\n') { fb_advance_line(); return; }
    if (c == '\r') { cur_x = 0; return; }
    if (c == '\b')
    {
        if (cur_x >= FB_FONT_WIDTH) cur_x -= FB_FONT_WIDTH;
        else if (cur_y >= FB_FONT_HEIGHT)
        {
            cur_y -= FB_FONT_HEIGHT;
            cur_x = fb_w >= FB_FONT_WIDTH ? (fb_w - FB_FONT_WIDTH) / FB_FONT_WIDTH * FB_FONT_WIDTH : 0;
        }
        fb_draw_char(cur_x, cur_y, ' ', color);
        return;
    }
    if ((unsigned char)c < 32U || (unsigned char)c >= 128U) return;
    if (fb_w < FB_FONT_WIDTH || fb_h < FB_FONT_HEIGHT) return;
    fb_draw_char(cur_x, cur_y, c, color);
    cur_x += FB_FONT_WIDTH;
    if (cur_x + FB_FONT_WIDTH > fb_w) fb_advance_line();
}

void fb_print(const char* s, uint32_t color)
{
    if (s == 0) return;
    while (*s) fb_putc(*s++, color);
}

void fb_draw_pixel(int x, int y, uint32_t color)
{
    if (!fb_ready || fb_base == 0 || x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    fb_base[(uint64_t)y * (uint64_t)fb_w + (uint64_t)x] = color;
}

void fb_draw_rect(int x, int y, int width, int height, uint32_t color)
{
    if (width <= 0 || height <= 0) return;
    fb_fill_rect(x, y, width, 1, color);
    fb_fill_rect(x, y + height - 1, width, 1, color);
    fb_fill_rect(x, y, 1, height, color);
    fb_fill_rect(x + width - 1, y, 1, height, color);
}

void fb_draw_round_rect(int x, int y, int width, int height, int radius, uint32_t color)
{
    if (!fb_ready || width <= 0 || height <= 0) return;
    int max_radius = width < height ? width / 2 : height / 2;
    if (radius < 0) radius = 0;
    if (radius > max_radius) radius = max_radius;
    if (radius == 0) { fb_fill_rect(x, y, width, height, color); return; }
    int r2 = radius * radius;
    for (int py = 0; py < height; ++py)
    {
        for (int px = 0; px < width; ++px)
        {
            int cx = px < radius ? radius - 1 : (px >= width - radius ? width - radius : px);
            int cy = py < radius ? radius - 1 : (py >= height - radius ? height - radius : py);
            int dx = px - cx, dy = py - cy;
            if ((px < radius || px >= width - radius) &&
                (py < radius || py >= height - radius) && dx * dx + dy * dy > r2)
                continue;
            fb_draw_pixel(x + px, y + py, color);
        }
    }
}

void fb_draw_circle(int cx, int cy, int radius, uint32_t color)
{
    if (radius < 0 || radius > 4096) return;
    int r2 = radius * radius;
    for (int y = -radius; y <= radius; ++y)
    {
        int rem = r2 - y * y;
        for (int x = -radius; x <= radius; ++x)
            if (x * x <= rem) fb_draw_pixel(cx + x, cy + y, color);
    }
}

void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy_abs = y1 >= y0 ? y1 - y0 : y0 - y1;
    int dy = -dy_abs;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 8192; ++guard)
    {
        fb_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static uint32_t blend_channel(uint32_t a, uint32_t b, int pos, int max)
{
    if (max <= 0) return b;
    int av = (int)a, bv = (int)b;
    return (uint32_t)((av * (max - pos) + bv * pos) / max);
}

void fb_fill_gradient(uint32_t top, uint32_t bottom)
{
    if (!fb_ready || fb_base == 0) return;
    int max = fb_h > 1 ? fb_h - 1 : 1;
    uint32_t tr = (top >> 16) & 255U, tg = (top >> 8) & 255U, tb = top & 255U;
    uint32_t br = (bottom >> 16) & 255U, bg = (bottom >> 8) & 255U, bb = bottom & 255U;
    for (int y = 0; y < fb_h; ++y)
    {
        uint32_t r = blend_channel(tr, br, y, max);
        uint32_t g = blend_channel(tg, bg, y, max);
        uint32_t b = blend_channel(tb, bb, y, max);
        uint32_t color = (r << 16) | (g << 8) | b;
        uint32_t* row = fb_base + (uint64_t)y * (uint64_t)fb_w;
        for (int x = 0; x < fb_w; ++x) row[x] = color;
    }
}

int fb_text_width(const char* text, int scale)
{
    if (text == 0) return 0;
    if (scale < 1) scale = 1;
    int length = 0;
    while (text[length] != '\0' && length < 1024) ++length;
    return length * 8 * scale;
}

void fb_draw_text(const char* text, int x, int y, uint32_t color, int scale)
{
    if (text == 0 || !fb_ready || fb_base == 0) return;
    if (scale < 1) scale = 1;
    if (scale > 12) scale = 12;
    for (int i = 0; text[i] != '\0' && i < 1024; ++i)
    {
        unsigned int c = (unsigned char)text[i];
        if (c >= 128U) c = (unsigned int)'?';
        for (int gy = 0; gy < 8; ++gy)
        {
            uint8_t bits = font8x8[c][gy];
            for (int gx = 0; gx < 8; ++gx)
            {
                if ((bits & (uint8_t)(0x80U >> gx)) == 0U) continue;
                fb_fill_rect(x + i * 8 * scale + gx * scale, y + gy * scale,
                             scale, scale, color);
            }
        }
    }
}

/*
 * UI-only antialiased text. The serial/framebuffer console retains its legacy
 * fixed-cell font. The UI uses a compact proportional Inter-derived coverage
 * table so labels are smoother and closer to modern mobile UI typography.
 */
static int ui_floor_div_256(int value)
{
    return value >= 0 ? value / 256 : -((-value + 255) / 256);
}

static uint32_t ui_font_source_pixel(unsigned int glyph, int x, int y)
{
    if (glyph >= 95U || x < 0 || x >= 32 || y < 0 || y >= 32) return 0;
    uint32_t pixel = (uint32_t)y * 32U + (uint32_t)x;
    uint8_t packed = ui_font_glyph_data[glyph][pixel >> 1];
    return (pixel & 1U) == 0U ? (uint32_t)(packed >> 4) : (uint32_t)(packed & 0x0FU);
}

static uint32_t ui_font_coverage(unsigned int glyph, int x, int y,
                                 int glyph_width, int glyph_height)
{
    int source_x_fp = (((2 * x + 1) * 32 * 128) / glyph_width) - 128;
    int source_y_fp = (((2 * y + 1) * 32 * 128) / glyph_height) - 128;
    int x0 = ui_floor_div_256(source_x_fp);
    int y0 = ui_floor_div_256(source_y_fp);
    int fx = source_x_fp - x0 * 256;
    int fy = source_y_fp - y0 * 256;
    uint32_t value = 0;
    for (int row = 0; row < 2; ++row)
    {
        int sy = y0 + row;
        uint32_t wy = row == 0 ? (uint32_t)(256 - fy) : (uint32_t)fy;
        for (int col = 0; col < 2; ++col)
        {
            int sx = x0 + col;
            uint32_t wx = col == 0 ? (uint32_t)(256 - fx) : (uint32_t)fx;
            value += ui_font_source_pixel(glyph, sx, sy) * wx * wy;
        }
    }
    /* Four-bit source coverage -> 8-bit framebuffer alpha. */
    return (value * 255U + 491520U) / 983040U;
}

static int ui_glyph_advance(unsigned int glyph, int height)
{
    if (glyph < 32U || glyph > 126U) glyph = (unsigned int)'?';
    int index = (int)glyph - 32;
    int advance = ((int)ui_font_advance[index] * height + 16) / 32;
    return advance > 0 ? advance : 1;
}

static uint32_t ui_blend_color(uint32_t foreground, uint32_t background, uint32_t alpha)
{
    uint32_t inverse = 255U - alpha;
    uint32_t r = ((((foreground >> 16) & 255U) * alpha) + (((background >> 16) & 255U) * inverse) + 127U) / 255U;
    uint32_t g = ((((foreground >> 8) & 255U) * alpha) + (((background >> 8) & 255U) * inverse) + 127U) / 255U;
    uint32_t b = (((foreground & 255U) * alpha) + ((background & 255U) * inverse) + 127U) / 255U;
    return (r << 16) | (g << 8) | b;
}

int fb_ui_text_width(const char* text, int scale)
{
    if (text == 0) return 0;
    if (scale < 1) scale = 1;
    if (scale > 12) scale = 12;
    int height = scale * 8 + 4;
    int width = 0;
    for (int i = 0; text[i] != '\0' && i < 1024; ++i)
    {
        unsigned int glyph = (unsigned char)text[i];
        width += ui_glyph_advance(glyph, height);
    }
    return width;
}

void fb_draw_ui_text(const char* text, int x, int y, uint32_t color, int scale)
{
    if (text == 0 || !fb_ready || fb_base == 0) return;
    if (scale < 1) scale = 1;
    if (scale > 12) scale = 12;
    const int glyph_height = scale * 8 + 4;
    int origin_x = x;
    for (int i = 0; text[i] != '\0' && i < 1024; ++i)
    {
        unsigned int glyph = (unsigned char)text[i];
        if (glyph < 32U || glyph > 126U) glyph = (unsigned int)'?';
        /* Render each source glyph on a uniformly scaled square cell. Its
           proportional advance leaves spacing based on the actual typeface. */
        const int glyph_width = glyph_height;
        const int advance = ui_glyph_advance(glyph, glyph_height);
        for (int gy = 0; gy < glyph_height; ++gy)
        {
            int py = y + gy;
            if (py < 0 || py >= fb_h) continue;
            for (int gx = 0; gx < glyph_width; ++gx)
            {
                int px = origin_x + gx;
                if (px < 0 || px >= fb_w) continue;
                uint32_t coverage = ui_font_coverage(glyph - 32U, gx, gy, glyph_width, glyph_height);
                if (coverage == 0U) continue;
                uint64_t offset = (uint64_t)py * (uint64_t)fb_w + (uint64_t)px;
                fb_base[offset] = ui_blend_color(color, fb_base[offset], coverage);
            }
        }
        origin_x += advance;
    }
}

/* Software mouse cursor (PS/2 mice are relative, so the guest draws its own).
   The pixels under the cursor are saved so it can be erased before a redraw. */
#define CUR_W 12
#define CUR_H 19
static uint32_t cursor_save[CUR_W * CUR_H];
static int cursor_x = 0, cursor_y = 0, cursor_visible = 0;
static const char* const cursor_shape[CUR_H] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ",
    "X.....X     ", "X......X    ", "X.......X   ", "X........X  ", "X.........X ", "X......XXXXX",
    "X...X..X    ", "X..X X..X   ", "X.X  X..X   ", "XX    X..X  ", "X     X..X  ", "       X..X ", "       XXX  "
};
void fb_cursor_hide()
{
    if (!cursor_visible || !fb_ready || fb_base == 0) { cursor_visible = 0; return; }
    for (int y = 0; y < CUR_H; ++y)
        for (int x = 0; x < CUR_W; ++x)
        {
            int px = cursor_x + x, py = cursor_y + y;
            if (px < 0 || py < 0 || px >= fb_w || py >= fb_h) continue;
            if (cursor_shape[y][x] == ' ') continue;
            fb_base[(uint64_t)py * (uint64_t)fb_w + (uint64_t)px] = cursor_save[y * CUR_W + x];
        }
    cursor_visible = 0;
}
void fb_cursor_show(int x, int y)
{
    if (!fb_ready || fb_base == 0) return;
    fb_cursor_hide();
    cursor_x = x; cursor_y = y;
    for (int cy = 0; cy < CUR_H; ++cy)
        for (int cx = 0; cx < CUR_W; ++cx)
        {
            int px = x + cx, py = y + cy;
            char shape = cursor_shape[cy][cx];
            if (shape == ' ') continue;
            if (px < 0 || py < 0 || px >= fb_w || py >= fb_h) continue;
            uint64_t off = (uint64_t)py * (uint64_t)fb_w + (uint64_t)px;
            cursor_save[cy * CUR_W + cx] = fb_base[off];
            fb_base[off] = shape == 'X' ? 0x000000U : 0xFFFFFFU;
        }
    cursor_visible = 1;
}

void fb_present()
{
    /* QEMU VGA LFB is directly visible; writes do not need a flush command. */
}

int fb_get_width() { return fb_w; }
int fb_get_height() { return fb_h; }
int fb_is_ready() { return fb_ready && fb_base != 0; }
int fb_has_display() { return fb_display_ready; }

#ifdef HOST_TEST
int fb_test_attach(uint32_t* pixels, int width, int height)
{
    if (pixels == 0 || width < FB_FONT_WIDTH || height < FB_FONT_HEIGHT || width > 4096 || height > 4096)
    {
        fb_base = 0; fb_ready = 0; fb_display_ready = 0; return -1;
    }
    fb_base = pixels; fb_w = width; fb_h = height;
    cur_x = 0; cur_y = 0; fb_ready = 1; fb_display_ready = 1;
    fb_clear(FB_BACKGROUND);
    return 0;
}
uint32_t fb_test_pixel(int x, int y)
{
    if (!fb_ready || fb_base == 0 || x < 0 || y < 0 || x >= fb_w || y >= fb_h) return 0;
    return fb_base[(uint64_t)y * (uint64_t)fb_w + (uint64_t)x];
}
#endif
