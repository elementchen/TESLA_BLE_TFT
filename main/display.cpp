#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>

// ─── ST7789 SPI LCD 引脚配置 ──────────────────────────────────────
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

    // 1. 初始化背光引脚
    gpio_config_t bk_gpio_config = {};
    bk_gpio_config.mode = GPIO_MODE_OUTPUT;
    bk_gpio_config.pin_bit_mask = 1ULL << DISPLAY_BLK;
    gpio_config(&bk_gpio_config);
    gpio_set_level((gpio_num_t)DISPLAY_BLK, 1); // 开启背光

    // 2. 初始化 SPI 总线
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = SCREEN_W * SCREEN_H * sizeof(uint16_t);
    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }

    // 3. 初始化 Panel IO SPI 通信
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
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        return false;
    }

    // 4. 初始化 ST7789 Panel 驱动
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISPLAY_RES;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    ret = esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ST7789 panel: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, false, true));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false)); // 正常彩色极性，物理黑底显示
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    initialized_ = true;
    
    // 清屏涂黑
    clear();
    
    ESP_LOGI(TAG, "ST7789 320x240 LCD initialization complete (No-cache Mode)");
    return true;
}


// ─── 绘图基础原语（直接操作 LCD 控制器） ─────────────────────────────

void Display::fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!panel_) return;
    // 裁剪边界
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    // 大端序翻转处理，保证颜色完美呈现
    uint16_t flipped_color = (color >> 8) | (color << 8);

    // 在内部 SRAM (BSS段) 中静态声明一行数据缓冲区，最大支持 320 像素宽
    static uint16_t fill_buf[320];
    for (int i = 0; i < w; i++) {
        fill_buf[i] = flipped_color;
    }

    // 分行高速写入到 ST7789 控制器
    for (int dy = 0; dy < h; dy++) {
        esp_lcd_panel_draw_bitmap(panel_, x, y + dy, x + w, y + dy + 1, fill_buf);
    }
}

// ─── 高清 Sans-Serif 字体绘制实现 ───────────────────────────────────

void Display::draw_char_8x16(int x, int y, char c, uint16_t color, uint16_t bg, bool use_bg, int scale) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font8x16_aa[c - 32];
    
    // 前景色的 RGB 分解
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    
    for (int row = 0; row < 16; row++) {
        const uint8_t *row_bytes = glyph + row * 4;
        for (int col = 0; col < 8; col++) {
            // 水平镜像纠正：物理列方向反转
            int col_read = 7 - col;
            int byte_idx = col_read / 2;
            uint8_t pixel_val = 0;
            if (col_read % 2 == 0) {
                pixel_val = row_bytes[byte_idx] >> 4;
            } else {
                pixel_val = row_bytes[byte_idx] & 0x0F;
            }
            
            if (pixel_val == 15) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            } else if (pixel_val > 0) {
                // 16级抗锯齿混色 (纯黑背景下等同于直接降采样亮度)
                uint8_t blend_r = (r * pixel_val) / 15;
                uint8_t blend_g = (g * pixel_val) / 15;
                uint8_t blend_b = (b * pixel_val) / 15;
                uint16_t blended_color = (blend_r << 11) | (blend_g << 5) | blend_b;
                fill_rect(x + col * scale, y + row * scale, scale, scale, blended_color);
            } else if (use_bg) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, bg);
            }
        }
    }
}

void Display::draw_text_8x16(int x, int y, const char *text, int len, uint16_t color, uint16_t bg, bool use_bg, int scale) {
    for (int i = 0; i < len; i++) {
        int cx = x + i * 8 * scale;
        if (cx + 8 * scale > SCREEN_W) break;
        draw_char_8x16(cx, y, text[i], color, bg, use_bg, scale);
    }
}

void Display::draw_char_24x48(int x, int y, char c, uint16_t color) {
    int idx = get_font24x48_index(c);
    const uint8_t *glyph = font24x48_nums_aa[idx];
    
    // 前景色的 RGB 分解
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    
    for (int row = 0; row < 48; row++) {
        const uint8_t *row_bytes = glyph + row * 12;
        for (int col = 0; col < 24; col++) {
            // 水平镜像纠正：物理列方向反转
            int col_read = 23 - col;
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
                // 16级抗锯齿混色
                uint8_t blend_r = (r * pixel_val) / 15;
                uint8_t blend_g = (g * pixel_val) / 15;
                uint8_t blend_b = (b * pixel_val) / 15;
                uint16_t blended_color = (blend_r << 11) | (blend_g << 5) | blend_b;
                fill_rect(x + col, y + row, 1, 1, blended_color);
            }
        }
    }
}

void Display::draw_text_24x48(int x, int y, const char *text, int len, uint16_t color) {
    for (int i = 0; i < len; i++) {
        int cx = x + i * 24;
        if (cx + 24 > SCREEN_W) break;
        draw_char_24x48(cx, y, text[i], color);
    }
}

void Display::clear() {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, 0x0000);
}

// ─── 界面高层绘制函数 ──────────────────────────────────────────────

void Display::show_splash() {
    if (!initialized_) return;
    clear();

    // Tesla Logo (红色, 8x16 放大 3 倍，超细腻)
    int xs = (SCREEN_W - 5 * 8 * 3) / 2;
    draw_text_8x16(xs, 75, "Tesla", 5, 0xF800, 0x0000, false, 3);

    // "BLE DASH" (白色, 8x16 放大 2 倍)
    xs = (SCREEN_W - 8 * 8 * 2) / 2;
    draw_text_8x16(xs, 140, "BLE DASH", 8, 0xFFFF, 0x0000, false, 2);

    // 底部小字
    xs = (SCREEN_W - 19 * 8) / 2;
    draw_text_8x16(xs, 200, "Initializing BLE...", 19, 0x7BEF);
}

void Display::show_pairing(const std::string &msg) {
    if (!initialized_) return;
    clear();

    // 顶部标题
    int xs = (SCREEN_W - 12 * 8 * 2) / 2;
    draw_text_8x16(xs, 25, "PAIRING MODE", 12, 0xF800, 0x0000, false, 2);

    // 绘制一把卡片钥匙 (居中于 320px，宽 100)
    fill_rect(110, 75, 100, 60, 0x18C3);
    for (int i = 0; i < 2; i++) {
        fill_rect(112 + i, 77 + i, 96 - 2 * i, 1, 0x7BEF);
        fill_rect(112 + i, 132 - i, 96 - 2 * i, 1, 0x7BEF);
        fill_rect(112 + i, 77 + i, 1, 56 - 2 * i, 0x7BEF);
        fill_rect(207 - i, 77 + i, 1, 56 - 2 * i, 0x7BEF);
    }
    draw_text_8x16(148, 97, "NFC", 3, 0xFFFF);

    // 底部刷卡提示
    xs = (SCREEN_W - (int)(msg.size() * 8)) / 2;
    draw_text_8x16(xs >= 0 ? xs : 0, 165, msg.c_str(), msg.size(), 0xFFFF);

    // 重置按键提示
    xs = (SCREEN_W - 26 * 8) / 2;
    draw_text_8x16(xs, 210, "Hold Boot button to cancel", 26, 0x7BEF);
}

void Display::show_error(const std::string &msg) {
    if (!initialized_) return;
    clear();

    int xs = (SCREEN_W - 5 * 8 * 2) / 2;
    draw_text_8x16(xs, 40, "ERROR", 5, 0xF800, 0x0000, false, 2);

    std::string line1 = msg.substr(0, 18);
    std::string line2 = msg.size() > 18 ? msg.substr(18, 18) : "";

    xs = (SCREEN_W - (int)(line1.size() * 8)) / 2;
    draw_text_8x16(xs >= 0 ? xs : 0, 100, line1.c_str(), line1.size(), 0xFFFF);

    if (!line2.empty()) {
        xs = (SCREEN_W - (int)(line2.size() * 8)) / 2;
        draw_text_8x16(xs >= 0 ? xs : 0, 130, line2.c_str(), line2.size(), 0xFFFF);
    }

    xs = (SCREEN_W - 17 * 8) / 2;
    draw_text_8x16(xs, 190, "Check connections", 17, 0x7BEF);
}

void Display::show_text_lines(const std::string &line1, const std::string &line2,
                               const std::string &line3) {
    if (!initialized_) return;
    clear();

    if (!line1.empty()) {
        int xs = (SCREEN_W - (int)(line1.size() * 8)) / 2;
        draw_text_8x16(xs >= 0 ? xs : 0, 50, line1.c_str(), line1.size(), 0xFFFF);
    }
    if (!line2.empty()) {
        int xs = (SCREEN_W - (int)(line2.size() * 8)) / 2;
        draw_text_8x16(xs >= 0 ? xs : 0, 110, line2.c_str(), line2.size(), 0xFFFF);
    }
    if (!line3.empty()) {
        int xs = (SCREEN_W - (int)(line3.size() * 8)) / 2;
        draw_text_8x16(xs >= 0 ? xs : 0, 170, line3.c_str(), line3.size(), 0xFFFF);
    }
}

// ─── 特斯拉仪表盘绘制逻辑 ──────────────────────────────────────────

void Display::draw_status_bar(const DashData &data) {
    // 绘制一条底部分割线
    fill_rect(10, 35, 300, 1, 0x18C3);

    // 1. 蓝牙连接状态
    uint16_t ble_color = data.ble_connected ? 0x03FF : 0x7BEF; // 蓝色 vs 灰色
    // 画一个状态指示圆点
    fill_rect(15, 16, 5, 5, ble_color);
    draw_text_8x16(25, 15, "BLE", 3, ble_color);
    if (data.ble_connected) {
        draw_text_8x16(53, 15, "OK", 2, 0x07E0); // 绿色 OK
    } else {
        draw_text_8x16(53, 15, "DISC", 4, 0x7BEF);
    }

    // 2. 车辆唤醒状态 (右移适配 320px)
    uint16_t awake_color = data.vehicle_awake ? 0x07E0 : 0x7BEF; // 绿色 vs 灰色
    fill_rect(210, 16, 5, 5, awake_color);
    if (data.vehicle_awake) {
        draw_text_8x16(220, 15, "VEHICLE AWAKE", 13, 0x07E0);
    } else {
        draw_text_8x16(220, 15, "VEHICLE SLEEP", 13, 0x7BEF);
    }
}

void Display::draw_speed(float speed) {
    int speed_int = (int)std::round(speed);
    if (speed_int > 999) speed_int = 999;
    if (speed_int < 0) speed_int = 0;

    char buf[8];
    int len = snprintf(buf, sizeof(buf), "%d", speed_int);

    // 使用科技感 24x48 高保真数字字库，时速极度圆滑
    int total_w = len * 24;
    int xs = (SCREEN_W - total_w) / 2;

    draw_text_24x48(xs, 50, buf, len, 0xFFFF);

    // 单位 "km/h"
    int unit_xs = (SCREEN_W - 32) / 2;
    draw_text_8x16(unit_xs, 115, "km/h", 4, 0xF800);
}

void Display::draw_energy_bar(float speed) {
    // y = 138 处的能量进度条 (拓宽至 260 像素)
    int bar_x = 30;
    int bar_y = 140;
    int bar_w = 260;
    int bar_h = 4;

    fill_rect(bar_x, bar_y, bar_w, bar_h, 0x18C3);

    float ratio = speed / 150.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    
    int fill_w = (int)(ratio * bar_w);
    if (fill_w > 0) {
        uint16_t color = 0x07FF;
        if (ratio > 0.85f) {
            color = 0xF800;
        } else if (ratio > 0.60f) {
            color = 0xFFE0;
        }
        fill_rect(bar_x, bar_y, fill_w, bar_h, color);
    }
}

void Display::draw_gears(char gear) {
    // 档位 P, R, N, D 在 320px 横屏下重新精细定位
    char gear_chars[] = {'P', 'R', 'N', 'D'};
    int gear_x[] = {38, 108, 178, 248};
    int gear_y = 165;
    int card_size = 32;

    for (int i = 0; i < 4; i++) {
        char g = gear_chars[i];
        int x = gear_x[i];
        
        if (gear == g) {
            // 当前档位：绘制红色高亮背景卡片，配合 8x16 缩放 2 倍 (16x32) 达到高清字体
            fill_rect(x, gear_y, card_size, card_size, 0xF800);
            draw_char_8x16(x + 8, gear_y, g, 0xFFFF, 0x0000, false, 2);
        } else {
            // 未选中档位：直接绘制暗灰色文字，无背景，高清细腻
            draw_char_8x16(x + 8, gear_y, g, 0x5AEB, 0x0000, false, 2);
        }
    }
}

void Display::draw_odometer(uint32_t odo) {
    char buf[32];
    int len;
    if (odo > 0) {
        len = snprintf(buf, sizeof(buf), "ODO: %u km", (unsigned int)odo);
    } else {
        len = snprintf(buf, sizeof(buf), "ODO: --- km");
    }

    int total_w = len * 8;
    int xs = (SCREEN_W - total_w) / 2;

    draw_text_8x16(xs, 205, buf, len, 0xCE79);
}

void Display::render_dashboard(const DashData &data) {
    if (!initialized_) return;

    if (!first_render_ && data.speed_kmh == last_data_.speed_kmh &&
        data.gear == last_data_.gear &&
        data.odometer_km == last_data_.odometer_km &&
        data.ble_connected == last_connected_ &&
        data.vehicle_awake == last_data_.vehicle_awake &&
        data.valid == last_data_.valid) {
        return;
    }

    clear();

    draw_status_bar(data);

    if (data.valid) {
        draw_speed(data.speed_kmh);
        draw_energy_bar(data.speed_kmh);
    } else {
        int xs = (SCREEN_W - 7 * 8 * 2) / 2;
        draw_text_8x16(xs, 75, "NO DATA", 7, 0x7BEF, 0x0000, false, 2);
    }

    draw_gears(data.gear);
    draw_odometer(data.odometer_km);

    last_data_ = data;
    last_connected_ = data.ble_connected;
    first_render_ = false;
}
