#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "images.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>

#define DISPLAY_SPI_SCK_PIN     21
#define DISPLAY_SPI_MOSI_PIN    47
#define DISPLAY_DC_PIN          40
#define DISPLAY_SPI_CS_PIN      41
#define DISPLAY_RES             45
#define DISPLAY_BLK             42

// 外部 1-bit raw 点阵小字库与 4-bit AA 中大字库声明
extern const uint8_t font6x12_raw[95][12];
extern const uint8_t font16x32_aa[95][256];
extern const uint8_t font40x80_nums_aa[18][1600];
int get_font40x80_index(char c);

Display::~Display() {
    if (panel_) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_) {
        esp_lcd_panel_io_del(panel_io_);
    }
    spi_bus_free(SPI3_HOST);
}

bool Display::init(int sda, int scl, int reset) {
    ESP_LOGI(TAG, "Initializing ST7789 SPI LCD...");

    gpio_config_t bk_gpio_config = {};
    bk_gpio_config.mode = GPIO_MODE_OUTPUT;
    bk_gpio_config.pin_bit_mask = 1ULL << DISPLAY_BLK;
    gpio_config(&bk_gpio_config);
    gpio_set_level((gpio_num_t)DISPLAY_BLK, 1);

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = SCREEN_W * SCREEN_H * sizeof(uint16_t);
    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        return false;
    }

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
    io_config.dc_gpio_num = DISPLAY_DC_PIN;
    io_config.spi_mode = 3;
    io_config.pclk_hz = 80 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ret = esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_);
    if (ret != ESP_OK) {
        return false;
    }

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISPLAY_RES;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    ret = esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_);
    if (ret != ESP_OK) {
        return false;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    initialized_ = true;
    clear();
    return true;
}

// ─── 离屏 Canvas 画布绘制辅助函数 ───────────────────────────────────

static void fill_rect_on_canvas(uint16_t *canvas, int canvas_w, int canvas_h, int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > canvas_w) w = canvas_w - x;
    if (y + h > canvas_h) h = canvas_h - y;
    if (w <= 0 || h <= 0) return;

    for (int dy = 0; dy < h; dy++) {
        int cy = y + dy;
        uint16_t *row_ptr = canvas + cy * canvas_w;
        for (int dx = 0; dx < w; dx++) {
            row_ptr[x + dx] = color;
        }
    }
}

static void draw_char_6x12_on_canvas(uint16_t *canvas, int canvas_w, int canvas_h, int x, int y, char c, uint16_t color) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font6x12_raw[c - 32];
    
    for (int row = 0; row < 12; row++) {
        int cy = y + row;
        if (cy < 0 || cy >= canvas_h) continue;
        uint8_t row_byte = glyph[row];
        uint16_t *row_ptr = canvas + cy * canvas_w;
        
        for (int col = 0; col < 6; col++) {
            int cx = x + col;
            if (cx < 0 || cx >= canvas_w) continue;
            
            bool bit_set = (row_byte & (1 << col)) != 0;
            if (bit_set) {
                row_ptr[cx] = color;
            }
        }
    }
}

static void draw_text_6x12_on_canvas(uint16_t *canvas, int canvas_w, int canvas_h, int x, int y, const char *text, int len, uint16_t color) {
    for (int i = 0; i < len; i++) {
        draw_char_6x12_on_canvas(canvas, canvas_w, canvas_h, x + i * 6, y, text[i], color);
    }
}

static void draw_char_16x32_on_canvas(uint16_t *canvas, int canvas_w, int canvas_h, int x, int y, char c, uint16_t color) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font16x32_aa[c - 32];
    
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    
    for (int row = 0; row < 32; row++) {
        int cy = y + row;
        if (cy < 0 || cy >= canvas_h) continue;
        const uint8_t *row_bytes = glyph + row * 8;
        uint16_t *row_ptr = canvas + cy * canvas_w;
        
        for (int col = 0; col < 16; col++) {
            int cx = x + col;
            if (cx < 0 || cx >= canvas_w) continue;
            
            int col_read = col;
            int byte_idx = col_read / 2;
            uint8_t pixel_val = 0;
            if (col_read % 2 == 0) {
                pixel_val = row_bytes[byte_idx] >> 4;
            } else {
                pixel_val = row_bytes[byte_idx] & 0x0F;
            }
            
            if (pixel_val == 15) {
                row_ptr[cx] = color;
            } else if (pixel_val > 0) {
                uint16_t bg_color = row_ptr[cx];
                uint8_t bg_r = (bg_color >> 11) & 0x1F;
                uint8_t bg_g = (bg_color >> 5) & 0x3F;
                uint8_t bg_b = bg_color & 0x1F;
                
                uint8_t blend_r = (r * pixel_val + bg_r * (15 - pixel_val)) / 15;
                uint8_t blend_g = (g * pixel_val + bg_g * (15 - pixel_val)) / 15;
                uint8_t blend_b = (b * pixel_val + bg_b * (15 - pixel_val)) / 15;
                row_ptr[cx] = (blend_r << 11) | (blend_g << 5) | blend_b;
            }
        }
    }
}

static void draw_char_40x80_on_canvas(uint16_t *canvas, int canvas_w, int canvas_h, int x, int y, char c, uint16_t color) {
    int idx = get_font40x80_index(c);
    const uint8_t *glyph = font40x80_nums_aa[idx];
    
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    
    for (int row = 0; row < 80; row++) {
        int cy = y + row;
        if (cy < 0 || cy >= canvas_h) continue;
        const uint8_t *row_bytes = glyph + row * 20;
        uint16_t *row_ptr = canvas + cy * canvas_w;
        
        for (int col = 0; col < 40; col++) {
            int cx = x + col;
            if (cx < 0 || cx >= canvas_w) continue;
            
            int col_read = col;
            int byte_idx = col_read / 2;
            uint8_t pixel_val = 0;
            if (col_read % 2 == 0) {
                pixel_val = row_bytes[byte_idx] >> 4;
            } else {
                pixel_val = row_bytes[byte_idx] & 0x0F;
            }
            
            if (pixel_val == 15) {
                row_ptr[cx] = color;
            } else if (pixel_val > 0) {
                uint16_t bg_color = row_ptr[cx];
                uint8_t bg_r = (bg_color >> 11) & 0x1F;
                uint8_t bg_g = (bg_color >> 5) & 0x3F;
                uint8_t bg_b = bg_color & 0x1F;
                
                uint8_t blend_r = (r * pixel_val + bg_r * (15 - pixel_val)) / 15;
                uint8_t blend_g = (g * pixel_val + bg_g * (15 - pixel_val)) / 15;
                uint8_t blend_b = (b * pixel_val + bg_b * (15 - pixel_val)) / 15;
                row_ptr[cx] = (blend_r << 11) | (blend_g << 5) | blend_b;
            }
        }
    }
}

static void draw_text_40x80_on_canvas(uint16_t *canvas, int canvas_w, int canvas_h, int x, int y, const char *text, int len, uint16_t color) {
    for (int i = 0; i < len; i++) {
        draw_char_40x80_on_canvas(canvas, canvas_w, canvas_h, x + i * 40, y, text[i], color);
    }
}

// ─── 基础原语 ──────────────────────────────────────────────────────

void Display::fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!panel_) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    uint16_t flipped_color = (color >> 8) | (color << 8);

    static uint16_t fill_buf[320];
    for (int i = 0; i < w; i++) {
        fill_buf[i] = flipped_color;
    }

    for (int dy = 0; dy < h; dy++) {
        esp_lcd_panel_draw_bitmap(panel_, x, y + dy, x + w, y + dy + 1, fill_buf);
    }
}

void Display::draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        fill_rect(x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Display::draw_rect_outline(int x, int y, int w, int h, uint16_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

void Display::draw_bitmap(int x, int y, int w, int h, const uint16_t *bitmap) {
    if (!panel_) return;
    if (x < 0 || y < 0 || x + w > SCREEN_W || y + h > SCREEN_H) return;

    static uint16_t draw_buf[14000];
    int total_pixels = w * h;
    if (total_pixels > 14000) return;

    for (int i = 0; i < total_pixels; i++) {
        uint16_t color = bitmap[i];
        draw_buf[i] = (color >> 8) | (color << 8);
    }

    esp_lcd_panel_draw_bitmap(panel_, x, y, x + w, y + h, draw_buf);
}

// ─── 1-bit 点阵小字库物理渲染实现 (去锯齿，边缘清晰清脆) ────────────

void Display::draw_char_6x12(int x, int y, char c, uint16_t color, uint16_t bg, bool use_bg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font6x12_raw[c - 32];
    
    for (int row = 0; row < 12; row++) {
        uint8_t row_byte = glyph[row];
        for (int col = 0; col < 6; col++) {
            bool bit_set = (row_byte & (1 << col)) != 0;
            if (bit_set) {
                fill_rect(x + col, y + row, 1, 1, color);
            } else if (use_bg) {
                fill_rect(x + col, y + row, 1, 1, bg);
            }
        }
    }
}

void Display::draw_text_6x12(int x, int y, const char *text, int len, uint16_t color, uint16_t bg, bool use_bg) {
    for (int i = 0; i < len; i++) {
        int cx = x + i * 6;
        if (cx + 6 > SCREEN_W) break;
        draw_char_6x12(cx, y, text[i], color, bg, use_bg);
    }
}

// 16x32 常规体 (大字抗锯齿保留)
void Display::draw_char_16x32(int x, int y, char c, uint16_t color, uint16_t bg, bool use_bg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font16x32_aa[c - 32];
    
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    
    for (int row = 0; row < 32; row++) {
        const uint8_t *row_bytes = glyph + row * 8;
        for (int col = 0; col < 16; col++) {
            int col_read = col;
            int byte_idx = col_read / 2;
            uint8_t pixel_val = 0;
            if (col_read % 2 == 0) {
                pixel_val = row_bytes[byte_idx] >> 4;
            } else {
                pixel_val = row_bytes[byte_idx] & 0x0F;
            }
            
            if (pixel_val == 15) {
                fill_rect(x + col, y + row, 1, 1, color);
            } else if (pixel_val > 0) {
                uint8_t blend_r = (r * pixel_val) / 15;
                uint8_t blend_g = (g * pixel_val) / 15;
                uint8_t blend_b = (b * pixel_val) / 15;
                uint16_t blended_color = (blend_r << 11) | (blend_g << 5) | blend_b;
                fill_rect(x + col, y + row, 1, 1, blended_color);
            } else if (use_bg) {
                fill_rect(x + col, y + row, 1, 1, bg);
            }
        }
    }
}

void Display::draw_text_16x32(int x, int y, const char *text, int len, uint16_t color, uint16_t bg, bool use_bg) {
    for (int i = 0; i < len; i++) {
        int cx = x + i * 16;
        if (cx + 16 > SCREEN_W) break;
        draw_char_16x32(cx, y, text[i], color, bg, use_bg);
    }
}

// 40x80 巨型字 (大字抗锯齿保留)
void Display::draw_char_40x80(int x, int y, char c, uint16_t color, uint16_t bg, bool use_bg) {
    int idx = get_font40x80_index(c);
    const uint8_t *glyph = font40x80_nums_aa[idx];
    
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    
    for (int row = 0; row < 80; row++) {
        const uint8_t *row_bytes = glyph + row * 20;
        for (int col = 0; col < 40; col++) {
            int col_read = col;
            int byte_idx = col_read / 2;
            uint8_t pixel_val = 0;
            if (col_read % 2 == 0) {
                pixel_val = row_bytes[byte_idx] >> 4;
            } else {
                pixel_val = row_bytes[byte_idx] & 0x0F;
            }
            
            if (pixel_val == 15) {
                fill_rect(x + col, y + row, 1, 1, color);
            } else if (pixel_val > 0) {
                uint8_t blend_r = (r * pixel_val) / 15;
                uint8_t blend_g = (g * pixel_val) / 15;
                uint8_t blend_b = (b * pixel_val) / 15;
                uint16_t blended_color = (blend_r << 11) | (blend_g << 5) | blend_b;
                fill_rect(x + col, y + row, 1, 1, blended_color);
            } else if (use_bg) {
                fill_rect(x + col, y + row, 1, 1, bg);
            }
        }
    }
}

void Display::draw_text_40x80(int x, int y, const char *text, int len, uint16_t color, uint16_t bg, bool use_bg) {
    for (int i = 0; i < len; i++) {
        int cx = x + i * 40;
        if (cx + 40 > SCREEN_W) break;
        draw_char_40x80(cx, y, text[i], color, bg, use_bg);
    }
}

// ─── 统一清屏 ──────────────────────────────────────────────────────

void Display::clear() {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, 0x0000);
}

void Display::show_splash() {
    if (!initialized_) return;
    clear();

    int xs = (SCREEN_W - 5 * 16) / 2;
    draw_text_16x32(xs, 75, "Tesla", 5, 0xF800);

    xs = (SCREEN_W - 8 * 16) / 2;
    draw_text_16x32(xs, 140, "BLE DASH", 8, 0xFFFF);

    xs = (SCREEN_W - 19 * 6) / 2;
    draw_text_6x12(xs, 200, "Initializing BLE...", 19, 0x7BEF);
}

void Display::show_pairing(const std::string &msg) {
    if (!initialized_) return;
    clear();

    int xs = (SCREEN_W - 12 * 16) / 2;
    draw_text_16x32(xs, 25, "PAIRING MODE", 12, 0xF800);

    fill_rect(110, 75, 100, 60, 0x18C3);
    draw_text_16x32(144, 88, "NFC", 3, 0xFFFF);

    xs = (SCREEN_W - (int)(msg.size() * 16)) / 2;
    draw_text_16x32(xs >= 0 ? xs : 0, 165, msg.c_str(), msg.size(), 0xFFFF);

    xs = (SCREEN_W - 26 * 6) / 2;
    draw_text_6x12(xs, 210, "Hold Boot button to cancel", 26, 0x7BEF);
}

void Display::show_error(const std::string &msg) {
    if (!initialized_) return;
    clear();

    int xs = (SCREEN_W - 5 * 16) / 2;
    draw_text_16x32(xs, 40, "ERROR", 5, 0xF800);

    std::string line1 = msg.substr(0, 18);
    std::string line2 = msg.size() > 18 ? msg.substr(18, 18) : "";

    xs = (SCREEN_W - (int)(line1.size() * 6)) / 2;
    draw_text_6x12(xs >= 0 ? xs : 0, 100, line1.c_str(), line1.size(), 0xFFFF);

    if (!line2.empty()) {
        xs = (SCREEN_W - (int)(line2.size() * 6)) / 2;
        draw_text_6x12(xs >= 0 ? xs : 0, 130, line2.c_str(), line2.size(), 0xFFFF);
    }

    xs = (SCREEN_W - 17 * 6) / 2;
    draw_text_6x12(xs, 190, "Check connections", 17, 0x7BEF);
}

void Display::show_text_lines(const std::string &line1, const std::string &line2,
                               const std::string &line3) {
    if (!initialized_) return;
    clear();

    if (!line1.empty()) {
        int xs = (SCREEN_W - (int)(line1.size() * 16)) / 2;
        draw_text_16x32(xs >= 0 ? xs : 0, 50, line1.c_str(), line1.size(), 0xFFFF);
    }
    if (!line2.empty()) {
        int xs = (SCREEN_W - (int)(line2.size() * 16)) / 2;
        draw_text_16x32(xs >= 0 ? xs : 0, 110, line2.c_str(), line2.size(), 0xFFFF);
    }
    if (!line3.empty()) {
        int xs = (SCREEN_W - (int)(line3.size() * 16)) / 2;
        draw_text_16x32(xs >= 0 ? xs : 0, 170, line3.c_str(), line3.size(), 0xFFFF);
    }
}

// ─── 特斯拉 UI 各块绘制函数 (状态栏贴边 Y=4, 割线 Y=20) ───────────────

void Display::draw_status_bar(const DashData &data) {
    fill_rect(10, 20, 300, 1, 0x18C3);

    uint16_t ble_color = data.ble_connected ? 0x03FF : 0x7BEF;
    fill_rect(15, 8, 4, 4, ble_color);
    draw_text_6x12(23, 4, "BLE", 3, ble_color);
    if (data.ble_connected) {
        draw_text_6x12(45, 4, "OK", 2, 0x07E0);
    } else {
        draw_text_6x12(45, 4, "DEMO", 4, 0xF9C0);
    }

    uint16_t lock_color = data.locked ? 0x07E0 : 0xFFE0;
    draw_text_6x12(130, 4, data.locked ? "LOCKED" : "UNLOCKED", data.locked ? 6 : 8, lock_color);

    uint16_t awake_color = data.vehicle_awake ? 0x07E0 : 0x7BEF;
    fill_rect(242, 8, 4, 4, awake_color);
    if (data.vehicle_awake) {
        draw_text_6x12(250, 4, "AWAKE", 5, 0x07E0);
    } else {
        draw_text_6x12(250, 4, "SLEEP", 5, 0x7BEF);
    }
}

void Display::draw_speed_or_gear(float speed, char gear) {}
void Display::draw_power_bar_dual(int x, int y, int w, int h, float power_kw) {}

void Display::draw_odometer(uint32_t odo) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "ODO: %u km", (unsigned int)odo);
    int xs = (SCREEN_W - len * 6) / 2;
    draw_text_6x12(xs, 222, buf, len, 0xCE79);
}

void Display::draw_car_chassis(int cx, int cy, const DashData &data) {}

// ─── 页面 1: 开门界面绘制实现 (大尺寸特斯拉红色实心位图) ───────────────

void Display::draw_closures_screen(const DashData &data) {
    if (is_first_render_) {
        clear();
        draw_status_bar(data);
        
        // 居中绘制 100x140 红色全开门特斯拉位图
        draw_bitmap(110, 51, 100, 140, car_open_image);
        
        // 绘制大红色警告文本 (Y=196)
        int xs = (SCREEN_W - 14 * 6) / 2;
        draw_text_6x12(xs, 196, "DOORS UNCLOSED", 14, 0xF800);
        
        // 里程贴底边 (Y=222)
        draw_odometer(data.odometer_km);
        
        is_first_render_ = false;
        last_data_ = data;
    }
    
    // 差分刷新
    if (data.ble_connected != last_data_.ble_connected ||
        data.locked != last_data_.locked ||
        data.vehicle_awake != last_data_.vehicle_awake) {
        fill_rect(0, 0, SCREEN_W, 19, 0x0000);
        draw_status_bar(data);
    }
    
    if (data.odometer_km != last_data_.odometer_km) {
        fill_rect(50, 220, 220, 20, 0x0000);
        draw_odometer(data.odometer_km);
    }
    
    last_data_ = data;
}

// ─── 页面 2: 极简充电界面绘制实现 (纯绿色无外廓进度条) ──────────────────

void Display::draw_charging_screen(const DashData &data) {
    if (is_first_render_) {
        clear();
        draw_status_bar(data);
        
        // 头部显示亮绿色 CHARGING 状态
        int xs = (SCREEN_W - 8 * 16) / 2;
        draw_text_16x32(xs, 35, "CHARGING", 8, 0x07E0);
        
        // 直槽充电进度底条背景 (X=30, Y=145, 宽 260, 高 8)
        fill_rect(30, 145, 260, 8, 0x18C3);
        
        draw_odometer(data.odometer_km);
        
        is_first_render_ = false;
        last_data_ = data;
    }
    
    // 充电差分刷新
    if (data.ble_connected != last_data_.ble_connected ||
        data.locked != last_data_.locked ||
        data.vehicle_awake != last_data_.vehicle_awake) {
        fill_rect(0, 0, SCREEN_W, 19, 0x0000);
        draw_status_bar(data);
    }
    
    // 充电中大字大包围区差分 (Y=72 到 140)
    if (std::abs(data.charge_power_kw - last_data_.charge_power_kw) > 0.1f ||
        data.battery_level != last_data_.battery_level ||
        data.minutes_to_charge_limit != last_data_.minutes_to_charge_limit ||
        data.battery_range_km != last_data_.battery_range_km) {
        
        fill_rect(10, 72, 300, 68, 0x0000);
        
        char buf[64];
        // 渲染 SOC + 充电功率 (如: "62% @ 7.2 kW")
        int len = snprintf(buf, sizeof(buf), "%d%% @ %.1f kW", (int)data.battery_level, data.charge_power_kw);
        int xs = (SCREEN_W - len * 16) / 2;
        draw_text_16x32(xs, 75, buf, len, 0xFFFF);
        
        // 渲染预计剩余时间和加电里程 (如: "Remaining: 3h 45m (+240 km)")
        int hrs = data.minutes_to_charge_limit / 60;
        int mins = data.minutes_to_charge_limit % 60;
        len = snprintf(buf, sizeof(buf), "Remaining: %dh %dm  (+%.0f km)", hrs, mins, data.battery_range_km);
        xs = (SCREEN_W - len * 6) / 2;
        draw_text_6x12(xs, 115, buf, len, 0xCE79);
    }
    
    // 极简亮绿色进度直槽覆盖 (Y=145)
    if (data.battery_level != last_data_.battery_level || last_data_.battery_level == 0) {
        fill_rect(30, 145, 260, 8, 0x18C3);
        int fill_w = (int)((data.battery_level / 100.0f) * 260.0f);
        if (fill_w > 260) fill_w = 260;
        fill_rect(30, 145, fill_w, 8, 0x07E0);
    }
    
    if (data.odometer_km != last_data_.odometer_km) {
        fill_rect(50, 220, 220, 20, 0x0000);
        draw_odometer(data.odometer_km);
    }
    
    last_data_ = data;
}

// ─── 页面 3: 驾驶主界面渲染实现 (差分刷新，大字时速，整合挡位) ───────────

void Display::draw_driving_screen(const DashData &data) {
    if (is_first_render_) {
        clear();
        draw_status_bar(data);
        
        // 胎压线框与电池图标底图
        draw_rect_outline(147, 184, 26, 30, 0x5AEB);
        draw_rect_outline(15, 186, 20, 10, 0xCE79);
        fill_rect(35, 188, 2, 6, 0xCE79);

        draw_odometer(data.odometer_km);

        is_first_render_ = false;
        last_data_ = data;
        memset(prev_drawn_speed_gear_str_, 0, sizeof(prev_drawn_speed_gear_str_));
    }

    // 1. 顶部状态栏
    if (data.ble_connected != last_data_.ble_connected ||
        data.locked != last_data_.locked ||
        data.vehicle_awake != last_data_.vehicle_awake) {
        fill_rect(0, 0, SCREEN_W, 19, 0x0000);
        draw_status_bar(data);
    }

    // 2. 车速与挡位融合大字 (260x80 Canvas, Y=32)
    int speed_int = (int)std::round(data.speed_kmh);
    char current_text[16] = {0};
    
    if (data.gear != prev_gear_) {
        prev_gear_ = data.gear;
        gear_switch_time_ms_ = esp_timer_get_time() / 1000;
    }
    int64_t now_ms = esp_timer_get_time() / 1000;
    
    bool show_gear_focus = (data.gear == 'P' || data.gear == 'R' || (now_ms - gear_switch_time_ms_ < 2000));

    if (show_gear_focus) {
        snprintf(current_text, sizeof(current_text), "G_%c", data.gear);
    } else {
        snprintf(current_text, sizeof(current_text), "S_%d", speed_int);
    }

    if (strcmp(current_text, prev_drawn_speed_gear_str_) != 0) {
        strcpy(prev_drawn_speed_gear_str_, current_text);
        
        static uint16_t speed_canvas[260 * 80];
        memset(speed_canvas, 0, sizeof(speed_canvas));
        
        if (show_gear_focus) {
            draw_char_40x80_on_canvas(speed_canvas, 260, 80, (260 - 40) / 2, 0, data.gear, 0xF800);
            draw_text_6x12_on_canvas(speed_canvas, 260, 80, (260 - 6 * 6) / 2, 68, "ACTIVE", 6, 0xF800);
        } else {
            // 行驶中：左侧画红色常规挡位 (16x32)，中间巨型 40x80 速度，右侧悬浮 km/h
            draw_char_16x32_on_canvas(speed_canvas, 260, 80, 15, 24, data.gear, 0xF800);
            
            char buf[8];
            int len = snprintf(buf, sizeof(buf), "%d", speed_int);
            int total_w = len * 40;
            int xs = (260 - total_w) / 2;
            draw_text_40x80_on_canvas(speed_canvas, 260, 80, xs, 0, buf, len, 0xFFFF);
            
            draw_text_6x12_on_canvas(speed_canvas, 260, 80, xs + total_w + 4, 52, "km/h", 4, 0x7BEF);
        }
        
        for (int i = 0; i < 260 * 80; i++) {
            uint16_t color = speed_canvas[i];
            speed_canvas[i] = (color >> 8) | (color << 8);
        }
        esp_lcd_panel_draw_bitmap(panel_, 30, 32, 30 + 260, 32 + 80, speed_canvas);
    }

    // 3. 双色功率条 (Y=128 到 154)
    if (std::abs(data.motor_power_kw - last_data_.motor_power_kw) > 0.3f) {
        static uint16_t power_canvas[260 * 26];
        memset(power_canvas, 0, sizeof(power_canvas));
        
        fill_rect_on_canvas(power_canvas, 260, 26, 0, 8, 260, 4, 0x18C3);
        int cx = 260 / 2;
        
        if (data.motor_power_kw > 0.0f) {
            float ratio = data.motor_power_kw / 120.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            int len = (int)(ratio * (260 / 2));
            if (len > 0) {
                fill_rect_on_canvas(power_canvas, 260, 26, cx, 8, len, 4, 0xF800);
            }
        } else if (data.motor_power_kw < 0.0f) {
            float ratio = -data.motor_power_kw / 60.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            int len = (int)(ratio * (260 / 2));
            if (len > 0) {
                fill_rect_on_canvas(power_canvas, 260, 26, cx - len, 8, len, 4, 0x07E0);
            }
        } else {
            fill_rect_on_canvas(power_canvas, 260, 26, cx - 1, 7, 3, 6, 0xFFFF);
        }
        
        char pwr_buf[16];
        int len_str = snprintf(pwr_buf, sizeof(pwr_buf), "%+.1f kW", data.motor_power_kw);
        int xs = (260 - len_str * 6) / 2;
        uint16_t val_color = (data.motor_power_kw >= 0.0f) ? 0xF800 : 0x07E0;
        draw_text_6x12_on_canvas(power_canvas, 260, 26, xs, 16, pwr_buf, len_str, val_color);
        
        for (int i = 0; i < 260 * 26; i++) {
            uint16_t color = power_canvas[i];
            power_canvas[i] = (color >> 8) | (color << 8);
        }
        esp_lcd_panel_draw_bitmap(panel_, 30, 128, 30 + 260, 128 + 26, power_canvas);
    }

    // 4. 左下角电池卡片盒 (X=12 到 110, Y=182 到 214)
    if (data.battery_level != last_data_.battery_level ||
        data.battery_range_km != last_data_.battery_range_km) {
        
        fill_rect(12, 182, 98, 32, 0x0000);
        
        draw_rect_outline(15, 186, 20, 10, 0xCE79);
        fill_rect(35, 188, 2, 6, 0xCE79);
        int fill_w = (int)((data.battery_level / 100.0f) * 16.0f);
        fill_rect(17, 188, fill_w, 6, 0x07E0);
        
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "%d%%", (int)data.battery_level);
        draw_text_6x12(40, 184, val_buf, strlen(val_buf), 0xFFFF);
        
        snprintf(val_buf, sizeof(val_buf), "%.0f km", data.battery_range_km);
        draw_text_6x12(15, 200, val_buf, strlen(val_buf), 0xCE79);
    }

    // 5. 右下角温度卡片盒 (X=230 到 315, Y=182 到 214)
    if (std::abs(data.inside_temp - last_data_.inside_temp) > 0.08f ||
        std::abs(data.outside_temp - last_data_.outside_temp) > 0.08f) {
        
        fill_rect(230, 182, 85, 32, 0x0000);
        
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "In:  %.1f C", data.inside_temp);
        draw_text_6x12(235, 184, val_buf, strlen(val_buf), 0xFFFF);
        snprintf(val_buf, sizeof(val_buf), "Out: %.1f C", data.outside_temp);
        draw_text_6x12(235, 200, val_buf, strlen(val_buf), 0xCE79);
    }

    // 6. 四角胎压卡片盒 (X=112 到 228, Y=179 到 214)
    if (std::abs(data.tpms_fl - last_data_.tpms_fl) > 0.05f ||
        std::abs(data.tpms_fr - last_data_.tpms_fr) > 0.05f ||
        std::abs(data.tpms_rl - last_data_.tpms_rl) > 0.05f ||
        std::abs(data.tpms_rr - last_data_.tpms_rr) > 0.05f) {
        
        fill_rect(112, 179, 116, 35, 0x0000);
        draw_rect_outline(147, 184, 26, 30, 0x5AEB);
        
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "%.1f", data.tpms_fl);
        draw_text_6x12(120, 187, val_buf, strlen(val_buf), 0xCE79);

        snprintf(val_buf, sizeof(val_buf), "%.1f", data.tpms_fr);
        draw_text_6x12(178, 187, val_buf, strlen(val_buf), 0xCE79);

        snprintf(val_buf, sizeof(val_buf), "%.1f", data.tpms_rl);
        draw_text_6x12(120, 201, val_buf, strlen(val_buf), 0xCE79);

        snprintf(val_buf, sizeof(val_buf), "%.1f", data.tpms_rr);
        draw_text_6x12(178, 201, val_buf, strlen(val_buf), 0xCE79);
    }

    // 7. 底部总里程卡片盒 (X=50 到 270, Y=220 到 238)
    if (data.odometer_km != last_data_.odometer_km) {
        fill_rect(50, 220, 220, 20, 0x0000);
        draw_odometer(data.odometer_km);
    }

    last_data_ = data;
}

// ─── 统一界面差分渲染分发接口 ──────────────────────────────────────────

void Display::render_dashboard(const DashData &data) {
    if (!initialized_) return;

    // 1. 根据数据确定当前所属的界面类型
    int current_screen_type = 0; // 0: driving, 1: closures, 2: charging
    
    if (data.valid) {
        bool any_door_open = (data.door_open_fl || data.door_open_fr ||
                              data.door_open_rl || data.door_open_rr ||
                              data.door_open_trunk_front || data.door_open_trunk_rear);
        
        if (any_door_open && (int)std::round(data.speed_kmh) == 0) {
            current_screen_type = 1; // closures
        } else if (data.charging && (int)std::round(data.speed_kmh) == 0) {
            current_screen_type = 2; // charging
        }
    }

    // 2. 检测到界面类型跳转，强制进行一次全局清屏和重置 is_first_render
    static int last_screen_type = -1;
    if (current_screen_type != last_screen_type) {
        last_screen_type = current_screen_type;
        is_first_render_ = true; // 强制重新初始化底图
    }

    // 3. 分发具体绘制
    if (current_screen_type == 1) {
        draw_closures_screen(data);
    } else if (current_screen_type == 2) {
        draw_charging_screen(data);
    } else {
        draw_driving_screen(data);
    }
}
