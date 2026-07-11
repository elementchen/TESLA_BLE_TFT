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
#define DISPLAY_SPI_SCK_PIN     39
#define DISPLAY_SPI_MOSI_PIN    40
#define DISPLAY_DC_PIN          38
#define DISPLAY_SPI_CS_PIN      41
#define DISPLAY_RES             45
#define DISPLAY_BLK             42

// ─── 5x7 Font Table (ASCII 32-126) ────────────────────────────────
const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // space
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x08,0x2A,0x1C,0x2A,0x08}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x00,0x08,0x14,0x22,0x41}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x41,0x22,0x14,0x08,0x00}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x01,0x01}, // F
    {0x3E,0x41,0x41,0x51,0x32}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x04,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x7F,0x20,0x18,0x20,0x7F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x03,0x04,0x78,0x04,0x03}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x00,0x00,0x7F,0x41,0x41}, // [
    {0x02,0x04,0x08,0x10,0x20}, // backslash
    {0x41,0x41,0x7F,0x00,0x00}, // ]
    {0x04,0x02,0x01,0x02,0x04}, // ^
    {0x40,0x40,0x40,0x40,0x40}, // _
    {0x00,0x01,0x02,0x04,0x00}, // `
    {0x20,0x54,0x54,0x54,0x78}, // a
    {0x7F,0x48,0x44,0x44,0x38}, // b
    {0x38,0x44,0x44,0x44,0x20}, // c
    {0x38,0x44,0x44,0x48,0x7F}, // d
    {0x38,0x54,0x54,0x54,0x18}, // e
    {0x08,0x7E,0x09,0x01,0x02}, // f
    {0x08,0x14,0x54,0x54,0x3C}, // g
    {0x7F,0x08,0x04,0x04,0x78}, // h
    {0x00,0x44,0x7D,0x40,0x00}, // i
    {0x20,0x40,0x44,0x3D,0x00}, // j
    {0x00,0x7F,0x10,0x28,0x44}, // k
    {0x00,0x41,0x7F,0x40,0x00}, // l
    {0x7C,0x04,0x18,0x04,0x78}, // m
    {0x7C,0x08,0x04,0x04,0x78}, // n
    {0x38,0x44,0x44,0x44,0x38}, // o
    {0x7C,0x14,0x14,0x14,0x08}, // p
    {0x08,0x14,0x14,0x18,0x7C}, // q
    {0x7C,0x08,0x04,0x04,0x08}, // r
    {0x48,0x54,0x54,0x54,0x20}, // s
    {0x04,0x3F,0x44,0x40,0x20}, // t
    {0x3C,0x40,0x40,0x20,0x7C}, // u
    {0x1C,0x20,0x40,0x20,0x1C}, // v
    {0x3C,0x40,0x30,0x40,0x3C}, // w
    {0x44,0x28,0x10,0x28,0x44}, // x
    {0x0C,0x50,0x50,0x50,0x3C}, // y
    {0x44,0x64,0x54,0x4C,0x44}, // z
    {0x00,0x08,0x36,0x41,0x00}, // {
    {0x00,0x00,0x7F,0x00,0x00}, // |
    {0x00,0x41,0x36,0x08,0x00}, // }
    {0x08,0x08,0x2A,0x1C,0x08}, // ~
};

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
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true)); // 反色适配
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    initialized_ = true;
    
    // 清屏涂黑
    clear();
    
    ESP_LOGI(TAG, "ST7789 240x240 LCD initialization complete (No-cache Mode)");
    return true;
}

void Display::flush() {
    // 兼容函数，在直写模式下不执行任何操作
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

    // 在内部 SRAM (BSS段) 中静态声明一行数据缓冲区，极大节约堆内存并避免 PSRAM 对齐崩溃
    static uint16_t fill_buf[240];
    for (int i = 0; i < w; i++) {
        fill_buf[i] = flipped_color;
    }

    // 分行高速写入到 ST7789 控制器
    for (int dy = 0; dy < h; dy++) {
        esp_lcd_panel_draw_bitmap(panel_, x, y + dy, x + w, y + dy + 1, fill_buf);
    }
}

void Display::draw_char_scaled(int x, int y, char c, int scale, uint16_t color, uint16_t bg, bool use_bg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font5x7[c - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            } else if (use_bg) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, bg);
            }
        }
    }
    // 字符间距列
    if (use_bg) {
        fill_rect(x + 5 * scale, y, scale, 7 * scale, bg);
    }
}

void Display::draw_text_scaled(int x, int y, const char *text, int len, int scale, uint16_t color, uint16_t bg, bool use_bg) {
    for (int i = 0; i < len; i++) {
        int cx = x + i * 6 * scale;
        if (cx + 5 * scale >= SCREEN_W) break;
        draw_char_scaled(cx, y, text[i], scale, color, bg, use_bg);
    }
}

void Display::clear() {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, 0x0000);
}

// ─── 界面高层绘制函数 ──────────────────────────────────────────────

void Display::show_splash() {
    if (!initialized_) return;
    clear();

    // Tesla Logo (红色, 大号字体)
    int xs = (SCREEN_W - 174) / 2;
    draw_text_scaled(xs, 75, "Tesla", 5, 6, 0xF800); // 赛道红

    // "BLE DASH" (白色, 中号字体)
    xs = (SCREEN_W - 94) / 2;
    draw_text_scaled(xs, 140, "BLE DASH", 8, 2, 0xFFFF);

    // 底部小字
    xs = (SCREEN_W - 130) / 2;
    draw_text_scaled(xs, 200, "Initializing BLE...", 19, 1, 0x7BEF);
}

void Display::show_pairing(const std::string &msg) {
    if (!initialized_) return;
    clear();

    // 顶部标题
    int xs = (SCREEN_W - 142) / 2;
    draw_text_scaled(xs, 25, "PAIRING MODE", 12, 2, 0xF800);

    // 绘制一把卡片钥匙
    fill_rect(70, 75, 100, 60, 0x18C3);
    for (int i = 0; i < 2; i++) {
        fill_rect(72 + i, 77 + i, 96 - 2 * i, 1, 0x7BEF);
        fill_rect(72 + i, 132 - i, 96 - 2 * i, 1, 0x7BEF);
        fill_rect(72 + i, 77 + i, 1, 56 - 2 * i, 0x7BEF);
        fill_rect(167 - i, 77 + i, 1, 56 - 2 * i, 0x7BEF);
    }
    draw_text_scaled(105, 95, "NFC", 3, 2, 0xFFFF);

    // 底部刷卡提示
    xs = (SCREEN_W - (int)(msg.size() * 12 - 2)) / 2;
    draw_text_scaled(xs >= 0 ? xs : 0, 165, msg.c_str(), msg.size(), 2, 0xFFFF);

    // 重置按键提示
    xs = (SCREEN_W - 166) / 2;
    draw_text_scaled(xs, 210, "Hold Boot button to cancel", 26, 1, 0x7BEF);
}

void Display::show_error(const std::string &msg) {
    if (!initialized_) return;
    clear();

    int xs = (SCREEN_W - 60) / 2;
    draw_text_scaled(xs, 40, "ERROR", 5, 2, 0xF800);

    std::string line1 = msg.substr(0, 18);
    std::string line2 = msg.size() > 18 ? msg.substr(18, 18) : "";

    xs = (SCREEN_W - (int)(line1.size() * 12 - 2)) / 2;
    draw_text_scaled(xs >= 0 ? xs : 0, 100, line1.c_str(), line1.size(), 2, 0xFFFF);

    if (!line2.empty()) {
        xs = (SCREEN_W - (int)(line2.size() * 12 - 2)) / 2;
        draw_text_scaled(xs >= 0 ? xs : 0, 130, line2.c_str(), line2.size(), 2, 0xFFFF);
    }

    xs = (SCREEN_W - 124) / 2;
    draw_text_scaled(xs, 190, "Check connections", 17, 1, 0x7BEF);
}

void Display::show_text_lines(const std::string &line1, const std::string &line2,
                               const std::string &line3) {
    if (!initialized_) return;
    clear();

    if (!line1.empty()) {
        int xs = (SCREEN_W - (int)(line1.size() * 12 - 2)) / 2;
        draw_text_scaled(xs >= 0 ? xs : 0, 50, line1.c_str(), line1.size(), 2, 0xFFFF);
    }
    if (!line2.empty()) {
        int xs = (SCREEN_W - (int)(line2.size() * 12 - 2)) / 2;
        draw_text_scaled(xs >= 0 ? xs : 0, 110, line2.c_str(), line2.size(), 2, 0xFFFF);
    }
    if (!line3.empty()) {
        int xs = (SCREEN_W - (int)(line3.size() * 12 - 2)) / 2;
        draw_text_scaled(xs >= 0 ? xs : 0, 170, line3.c_str(), line3.size(), 2, 0xFFFF);
    }
}

// ─── 特斯拉仪表盘绘制逻辑 ──────────────────────────────────────────

void Display::draw_status_bar(const DashData &data) {
    fill_rect(10, 35, 220, 1, 0x18C3);

    // 1. 蓝牙连接状态
    uint16_t ble_color = data.ble_connected ? 0x03FF : 0x7BEF;
    fill_rect(15, 16, 5, 5, ble_color);
    draw_text_scaled(25, 15, "BLE", 3, 1, ble_color);
    if (data.ble_connected) {
        draw_text_scaled(47, 15, "OK", 2, 1, 0x07E0);
    } else {
        draw_text_scaled(47, 15, "DISC", 4, 1, 0x7BEF);
    }

    // 2. 车辆唤醒状态
    uint16_t awake_color = data.vehicle_awake ? 0x07E0 : 0x7BEF;
    fill_rect(155, 16, 5, 5, awake_color);
    if (data.vehicle_awake) {
        draw_text_scaled(165, 15, "VEHICLE AWAKE", 13, 1, 0x07E0);
    } else {
        draw_text_scaled(165, 15, "VEHICLE SLEEP", 13, 1, 0x7BEF);
    }
}

void Display::draw_speed(float speed) {
    int speed_int = (int)std::round(speed);
    if (speed_int > 999) speed_int = 999;
    if (speed_int < 0) speed_int = 0;

    char buf[8];
    int len = snprintf(buf, sizeof(buf), "%d", speed_int);

    int char_w = 40;
    int gap = 8;
    int total_w = len * (char_w + gap) - gap;
    int xs = (SCREEN_W - total_w) / 2;

    draw_text_scaled(xs, 50, buf, len, 8, 0xFFFF);

    int unit_xs = (SCREEN_W - 46) / 2;
    draw_text_scaled(unit_xs, 115, "km/h", 4, 2, 0xF800);
}

void Display::draw_energy_bar(float speed) {
    int bar_x = 30;
    int bar_y = 140;
    int bar_w = 180;
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
    char gear_chars[] = {'P', 'R', 'N', 'D'};
    int gear_x[] = {30, 82, 134, 186};
    int gear_y = 160;
    int card_size = 28;

    for (int i = 0; i < 4; i++) {
        char g = gear_chars[i];
        int x = gear_x[i];
        
        if (gear == g) {
            fill_rect(x, gear_y, card_size, card_size, 0xF800);
            draw_char_scaled(x + 7, gear_y + 4, g, 3, 0xFFFF);
        } else {
            draw_char_scaled(x + 7, gear_y + 4, g, 3, 0x5AEB);
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

    int total_w = len * 12 - 2;
    int xs = (SCREEN_W - total_w) / 2;

    draw_text_scaled(xs, 205, buf, len, 2, 0xCE79);
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
        int xs = (SCREEN_W - 82) / 2;
        draw_text_scaled(xs, 75, "NO DATA", 7, 2, 0x7BEF);
    }

    draw_gears(data.gear);
    draw_odometer(data.odometer_km);

    last_data_ = data;
    last_connected_ = data.ble_connected;
    first_render_ = false;
}
