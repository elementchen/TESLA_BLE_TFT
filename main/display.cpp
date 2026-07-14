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

// ─── 绘图基础原语 ──────────────────────────────────────────────────

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

// ─── 高清 Sans-Serif 字体绘制实现 ──────────────────────────────────

void Display::draw_char_6x12(int x, int y, char c, uint16_t color, uint16_t bg, bool use_bg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font6x12_aa[c - 32];
    
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    
    for (int row = 0; row < 12; row++) {
        const uint8_t *row_bytes = glyph + row * 3;
        for (int col = 0; col < 6; col++) {
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

void Display::draw_text_6x12(int x, int y, const char *text, int len, uint16_t color, uint16_t bg, bool use_bg) {
    for (int i = 0; i < len; i++) {
        int cx = x + i * 6;
        if (cx + 6 > SCREEN_W) break;
        draw_char_6x12(cx, y, text[i], color, bg, use_bg);
    }
}

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

// 32x64 覆盖背景绘制
void Display::draw_char_32x64(int x, int y, char c, uint16_t color, uint16_t bg, bool use_bg) {
    int idx = get_font32x64_index(c);
    const uint8_t *glyph = font32x64_nums_aa[idx];
    
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    
    for (int row = 0; row < 64; row++) {
        const uint8_t *row_bytes = glyph + row * 16;
        for (int col = 0; col < 32; col++) {
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

void Display::draw_text_32x64(int x, int y, const char *text, int len, uint16_t color, uint16_t bg, bool use_bg) {
    for (int i = 0; i < len; i++) {
        int cx = x + i * 32;
        if (cx + 32 > SCREEN_W) break;
        draw_char_32x64(cx, y, text[i], color, bg, use_bg);
    }
}

// ─── 启动与底色 ────────────────────────────────────────────────────

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

// ─── 特斯拉 UI 功能块定义 ──────────────────────────────────────────

void Display::draw_status_bar(const DashData &data) {
    fill_rect(10, 30, 300, 1, 0x18C3);

    uint16_t ble_color = data.ble_connected ? 0x03FF : 0x7BEF;
    fill_rect(15, 14, 4, 4, ble_color);
    draw_text_6x12(23, 10, "BLE", 3, ble_color);
    if (data.ble_connected) {
        draw_text_6x12(45, 10, "OK", 2, 0x07E0);
    } else {
        draw_text_6x12(45, 10, "DEMO", 4, 0xF9C0);
    }

    uint16_t lock_color = data.locked ? 0x07E0 : 0xFFE0;
    draw_text_6x12(130, 10, data.locked ? "LOCKED" : "UNLOCKED", data.locked ? 6 : 8, lock_color);

    uint16_t awake_color = data.vehicle_awake ? 0x07E0 : 0x7BEF;
    fill_rect(242, 14, 4, 4, awake_color);
    if (data.vehicle_awake) {
        draw_text_6x12(250, 10, "AWAKE", 5, 0x07E0);
    } else {
        draw_text_6x12(250, 10, "SLEEP", 5, 0x7BEF);
    }
}

void Display::draw_speed_or_gear(float speed, char gear) {
    // 基础绘制逻辑被 render_dashboard 的局部差分擦除逻辑接管
}

void Display::draw_power_bar_dual(int x, int y, int w, int h, float power_kw) {
    fill_rect(x, y, w, h, 0x18C3);
    int cx = x + w / 2;
    
    if (power_kw > 0.0f) {
        float ratio = power_kw / 120.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        int len = (int)(ratio * (w / 2));
        if (len > 0) {
            fill_rect(cx, y, len, h, 0xF800);
        }
    } else if (power_kw < 0.0f) {
        float ratio = -power_kw / 60.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        int len = (int)(ratio * (w / 2));
        if (len > 0) {
            fill_rect(cx - len, y, len, h, 0x07E0);
        }
    } else {
        fill_rect(cx - 1, y - 1, 3, h + 2, 0xFFFF);
    }

    char buf[16];
    int len_str = snprintf(buf, sizeof(buf), "%+.1f kW", power_kw);
    int xs = (SCREEN_W - len_str * 6) / 2;
    uint16_t val_color = (power_kw >= 0.0f) ? 0xF800 : 0x07E0;
    draw_text_6x12(xs, y + 8, buf, len_str, val_color);
}

void Display::draw_gears(char gear) {
    char gear_chars[] = {'P', 'R', 'N', 'D', 'S'};
    int gear_x[] = {35, 90, 145, 200, 255};
    int gear_y = 158;
    int card_size = 26;

    for (int i = 0; i < 5; i++) {
        char g = gear_chars[i];
        int x = gear_x[i];
        
        if (gear == g) {
            fill_rect(x, gear_y, card_size, card_size, 0xF800);
            draw_char_16x32(x + 5, gear_y - 3, g, 0xFFFF);
        } else {
            // 普通卡片擦除背景，防止残留红底
            fill_rect(x, gear_y, card_size, card_size, 0x0000);
            draw_char_16x32(x + 5, gear_y - 3, g, 0x5AEB);
        }
    }
}

void Display::draw_odometer(uint32_t odo) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "ODO: %u km", (unsigned int)odo);
    int xs = (SCREEN_W - len * 6) / 2;
    draw_text_6x12(xs, 208, buf, len, 0xCE79);
}

// ─── 备用占位 ──────────────────────────────────────────────────────

void Display::draw_car_chassis(int cx, int cy, const DashData &data) {}
void Display::draw_driving_screen(const DashData &data) {}
void Display::draw_closures_screen(const DashData &data) {}
void Display::draw_charging_screen(const DashData &data) {}

// ─── 统一界面差分渲染入口 (黄金车规级 0 闪写字擦除架构) ───────────────

void Display::render_dashboard(const DashData &data) {
    if (!initialized_) return;

    // 1. 无效/掉线状态
    if (!data.valid) {
        if (is_first_render_ || last_data_.valid) {
            clear();
            draw_status_bar(data);
            int xs = (SCREEN_W - 7 * 16) / 2;
            draw_text_16x32(xs, 75, "NO DATA", 7, 0x7BEF);
        }
        last_data_ = data;
        is_first_render_ = false;
        return;
    }

    // 2. 首次渲染：全屏铺底色与静态外框，为后续局部微刷打下基础
    if (is_first_render_) {
        clear();
        draw_status_bar(data);
        
        // 胎压线框
        draw_rect_outline(147, 170, 26, 30, 0x5AEB);
        // 电池外框
        draw_rect_outline(15, 172, 20, 10, 0xCE79);
        fill_rect(35, 174, 2, 6, 0xCE79);

        // ODO 里程
        draw_odometer(data.odometer_km);
        
        // 挡位指示背景
        draw_gears(data.gear);

        is_first_render_ = false;
        last_data_ = data;
        memset(prev_drawn_speed_gear_str_, 0, sizeof(prev_drawn_speed_gear_str_));
    }

    // ─── C. 核心局部差分重绘 ───

    // 1) 顶部状态栏
    if (data.ble_connected != last_data_.ble_connected ||
        data.locked != last_data_.locked ||
        data.vehicle_awake != last_data_.vehicle_awake) {
        fill_rect(0, 0, SCREEN_W, 29, 0x0000);
        draw_status_bar(data);
    }

    // 2) 挡位下方大框 (切换挡位)
    if (data.gear != last_data_.gear) {
        draw_gears(data.gear);
    }

    // 3) 车速与挡位特写大字区 (Y=38 到 116)
    int speed_int = (int)std::round(data.speed_kmh);
    char current_text[16] = {0};
    
    // 挡位改变检测
    if (data.gear != prev_gear_) {
        prev_gear_ = data.gear;
        gear_switch_time_ms_ = esp_timer_get_time() / 1000;
    }
    int64_t now_ms = esp_timer_get_time() / 1000;
    
    // D档刚起步前 2 秒显示特写，P档和R档静止时强制显示特写大字
    bool show_gear_focus = (data.gear == 'P' || data.gear == 'R' || (now_ms - gear_switch_time_ms_ < 2000));

    if (show_gear_focus) {
        snprintf(current_text, sizeof(current_text), "G_%c", data.gear);
    } else {
        snprintf(current_text, sizeof(current_text), "S_%d", speed_int);
    }

    // 当大字字符串改变时，才重刷正中大字区
    if (strcmp(current_text, prev_drawn_speed_gear_str_) != 0) {
        strcpy(prev_drawn_speed_gear_str_, current_text);
        
        // 极速局域涂黑 (宽 240, 高 80，刷新极快，100% 毫无闪烁)
        fill_rect(30, 38, 260, 80, 0x0000);
        
        if (show_gear_focus) {
            int xs = (SCREEN_W - 32) / 2;
            draw_char_32x64(xs, 40, data.gear, 0xF800);
            draw_text_6x12(142, 108, "ACTIVE", 6, 0xF800);
        } else {
            char buf[8];
            int len = snprintf(buf, sizeof(buf), "%d", speed_int);
            int total_w = len * 32;
            int xs = (SCREEN_W - total_w) / 2;
            
            draw_text_32x64(xs, 40, buf, len, 0xFFFF);
            draw_text_6x12(xs + total_w + 4, 80, "km/h", 4, 0x7BEF);
        }
    }

    // 4) 双色功率能量回收条 (变化偏差 > 0.3 kW 时触发局部重刷)
    if (std::abs(data.motor_power_kw - last_data_.motor_power_kw) > 0.3f) {
        fill_rect(30, 130, 260, 26, 0x0000);
        draw_power_bar_dual(30, 138, 260, 4, data.motor_power_kw);
    }

    // 5) 左下角电池与公里数卡片
    if (data.battery_level != last_data_.battery_level ||
        data.battery_range_km != last_data_.battery_range_km) {
        
        fill_rect(17, 174, 16, 6, 0x0000);
        int fill_w = (int)((data.battery_level / 100.0f) * 16.0f);
        fill_rect(17, 174, fill_w, 6, 0x07E0);
        
        fill_rect(40, 170, 70, 12, 0x0000);
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "%d%%", (int)data.battery_level);
        draw_text_6x12(40, 170, val_buf, strlen(val_buf), 0xFFFF);
        
        fill_rect(15, 186, 95, 12, 0x0000);
        snprintf(val_buf, sizeof(val_buf), "%.0f km", data.battery_range_km);
        draw_text_6x12(15, 186, val_buf, strlen(val_buf), 0xCE79);
    }

    // 6) 右下角温度卡片
    if (std::abs(data.inside_temp - last_data_.inside_temp) > 0.08f ||
        std::abs(data.outside_temp - last_data_.outside_temp) > 0.08f) {
        
        fill_rect(235, 170, 80, 28, 0x0000);
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "In:  %.1f C", data.inside_temp);
        draw_text_6x12(235, 170, val_buf, strlen(val_buf), 0xFFFF);
        snprintf(val_buf, sizeof(val_buf), "Out: %.1f C", data.outside_temp);
        draw_text_6x12(235, 186, val_buf, strlen(val_buf), 0xCE79);
    }

    // 7) 四轮胎压数据卡片
    if (std::abs(data.tpms_fl - last_data_.tpms_fl) > 0.05f ||
        std::abs(data.tpms_fr - last_data_.tpms_fr) > 0.05f ||
        std::abs(data.tpms_rl - last_data_.tpms_rl) > 0.05f ||
        std::abs(data.tpms_rr - last_data_.tpms_rr) > 0.05f) {
        
        fill_rect(110, 165, 35, 33, 0x0000);
        fill_rect(175, 165, 45, 33, 0x0000);
        
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "%.1f", data.tpms_fl);
        draw_text_6x12(120, 168, val_buf, strlen(val_buf), 0xCE79);

        snprintf(val_buf, sizeof(val_buf), "%.1f", data.tpms_fr);
        draw_text_6x12(178, 168, val_buf, strlen(val_buf), 0xCE79);

        snprintf(val_buf, sizeof(val_buf), "%.1f", data.tpms_rl);
        draw_text_6x12(120, 186, val_buf, strlen(val_buf), 0xCE79);

        snprintf(val_buf, sizeof(val_buf), "%.1f", data.tpms_rr);
        draw_text_6x12(178, 186, val_buf, strlen(val_buf), 0xCE79);
    }

    // 8) 底部里程卡片
    if (data.odometer_km != last_data_.odometer_km) {
        fill_rect(60, 205, 200, 18, 0x0000);
        draw_odometer(data.odometer_km);
    }

    // 状态更新
    last_data_ = data;
}
