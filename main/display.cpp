#include "display.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>

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

// ─── I2C / SSD1306 Low-Level ──────────────────────────────────────

Display::~Display() {
    if (i2c_dev_handle_) {
        i2c_master_bus_rm_device(i2c_dev_handle_);
    }
    if (i2c_bus_handle_) {
        i2c_del_master_bus(i2c_bus_handle_);
    }
}

bool Display::init(int sda, int scl, int reset) {
    // Configure I2C bus
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_1;
    bus_cfg.sda_io_num = (gpio_num_t)sda;
    bus_cfg.scl_io_num = (gpio_num_t)scl;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &i2c_bus_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %d", ret);
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = I2C_ADDR;
    dev_cfg.scl_speed_hz = 100000;

    ret = i2c_master_bus_add_device(i2c_bus_handle_, &dev_cfg, &i2c_dev_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %d", ret);
        return false;
    }

    i2c_port_ = 1;

    // SSD1306 init sequence for 128x32
    static const uint8_t init_cmds[] = {
        0xAE,       // display off
        0xD5, 0x80, // clock div
        0xA8, 0x1F, // multiplex ratio = 31 (32-1)
        0xD3, 0x00, // display offset
        0x40,       // start line
        0x8D, 0x14, // charge pump enable
        0x20, 0x00, // memory mode: horizontal
        0xA1,       // segment remap (column 127 = SEG0)
        0xC8,       // COM scan direction (COM[N-1] to COM0)
        0xDA, 0x02, // COM pins
        0x81, 0x8F, // contrast
        0xD9, 0xF1, // pre-charge
        0xDB, 0x40, // VCOM detect
        0xA4,       // entire display on: follow RAM
        0xA6,       // normal (not inverted)
        0xAF,       // display on
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        i2c_write_cmd(init_cmds[i]);
    }

    memset(framebuf_, 0, sizeof(framebuf_));
    flush();

    initialized_ = true;
    ESP_LOGI(TAG, "SSD1306 I2C init OK (SDA=%d SCL=%d %dx%d)", sda, scl, SCREEN_W, SCREEN_H);
    return true;
}

void Display::i2c_write_cmd(uint8_t cmd) {
    if (!i2c_dev_handle_) return;
    uint8_t buf[2] = {0x00, cmd};  // 0x00 = command mode
    i2c_master_transmit(i2c_dev_handle_, buf, 2, pdMS_TO_TICKS(10));
}

void Display::i2c_write_data(const uint8_t *data, size_t len) {
    if (!i2c_dev_handle_) return;
    // I2C writes are limited; split into chunks with 0x40 prefix
    static uint8_t buf[129];  // 1 + max 128 bytes
    buf[0] = 0x40;  // data mode
    while (len > 0) {
        size_t chunk = len > 128 ? 128 : len;
        memcpy(buf + 1, data, chunk);
        i2c_master_transmit(i2c_dev_handle_, buf, chunk + 1, pdMS_TO_TICKS(20));
        data += chunk;
        len -= chunk;
    }
}

void Display::flush() {
    // Send entire framebuffer in one batch
    uint8_t col_cmd[] = {0x00, 0x21, 0x00, 0x7F};  // column 0-127
    uint8_t page_cmd[] = {0x00, 0x22, 0x00, 0x03};  // page 0-3 (128x32)
    i2c_write_cmd(0x21); i2c_write_cmd(0x00); i2c_write_cmd(0x7F);
    i2c_write_cmd(0x22); i2c_write_cmd(0x00); i2c_write_cmd(0x03);
    i2c_write_data(framebuf_, sizeof(framebuf_));
}

// ─── Drawing Primitives ───────────────────────────────────────────

static inline void set_pixel(uint8_t *fb, int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 32) return;
    fb[x + (y / 8) * 128] |= (1 << (y & 7));
}

static inline void clear_pixel(uint8_t *fb, int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 32) return;
    fb[x + (y / 8) * 128] &= ~(1 << (y & 7));
}

void Display::draw_char_5x7(int x, int y, char c, bool invert) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font5x7[c - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = invert ? ~glyph[col] : glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row))
                set_pixel(framebuf_, x + col, y + row);
            else
                clear_pixel(framebuf_, x + col, y + row);
        }
    }
    // 1px spacing column
    for (int row = 0; row < 7; row++)
        clear_pixel(framebuf_, x + 5, y + row);
}

void Display::draw_char_15x21(int x, int y, char c) {
    // 3x scale of 5x7 font (15x21 pixels)
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font5x7[c - 32];
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (glyph[col] & (1 << row)) {
                for (int dy = 0; dy < 3; dy++)
                    for (int dx = 0; dx < 3; dx++)
                        set_pixel(framebuf_, x + col*3 + dx, y + row*3 + dy);
            }
        }
    }
}

void Display::draw_text(int x, int y, const char *text, int len, bool invert) {
    for (int i = 0; i < len && x + i*6 < SCREEN_W - 5; i++) {
        draw_char_5x7(x + i*6, y, text[i], invert);
    }
}

void Display::draw_text_x3(int x, int y, const char *text, int len) {
    for (int i = 0; i < len && x + i*18 < SCREEN_W - 15; i++) {
        draw_char_15x21(x + i*18, y, text[i]);
    }
}

// ─── High-Level Display Methods ───────────────────────────────────

void Display::clear() {
    memset(framebuf_, 0, sizeof(framebuf_));
    flush();
}

void Display::show_splash() {
    if (!initialized_) return;
    memset(framebuf_, 0, sizeof(framebuf_));
    draw_text_x3(12, 4, "Tesla", 5);
    draw_text(48, 24, "BLE Dash", 8);
    flush();
}

void Display::show_pairing(const std::string &msg) {
    if (!initialized_) return;
    memset(framebuf_, 0, sizeof(framebuf_));
    draw_text_x3(4, 0, "PAIR", 4);
    draw_text(0, 24, msg.c_str(), msg.size() < 21 ? (int)msg.size() : 21);
    flush();
}

void Display::show_error(const std::string &msg) {
    if (!initialized_) return;
    memset(framebuf_, 0, sizeof(framebuf_));
    draw_text(0, 0, "ERR", 3);
    draw_text(0, 10, msg.c_str(), msg.size() < 21 ? (int)msg.size() : 21);
    draw_text(0, 20, msg.size() > 21 ? msg.c_str() + 21 : "", msg.size() > 21 ? (int)(msg.size() - 21) : 0);
    flush();
}

void Display::show_text_lines(const std::string &line1, const std::string &line2,
                               const std::string &line3) {
    if (!initialized_) return;
    memset(framebuf_, 0, sizeof(framebuf_));
    if (!line1.empty()) draw_text(0, 0,  line1.c_str(), line1.size());
    if (!line2.empty()) draw_text(0, 10, line2.c_str(), line2.size());
    if (!line3.empty()) draw_text(0, 20, line3.c_str(), line3.size());
    flush();
}

// ─── Dashboard Render (128x32) ────────────────────────────────────
//
//  y=0..20: Speed x3 (21px tall)
//  y=24..31: [Gear] ODO status line (7px tall)
//
//  ┌──────────────────────────────┐
//  │        88 km/h          BT ON│
//  │ [D] ODO: 42,350 km           │
//  └──────────────────────────────┘

void Display::render_dashboard(const DashData &data) {
    if (!initialized_) return;

    if (!first_render_ && data.speed_kmh == last_data_.speed_kmh &&
        data.gear == last_data_.gear &&
        data.odometer_km == last_data_.odometer_km &&
        data.ble_connected == last_connected_) {
        return;
    }

    memset(framebuf_, 0, sizeof(framebuf_));

    // ── Speed (x3 large font, y=0, centered) ──
    int speed_int = (int)std::round(data.speed_kmh);
    if (speed_int > 999) speed_int = 999;

    char buf[8];
    int len = snprintf(buf, sizeof(buf), "%d", speed_int);

    // Center 3 digits at (128 - len*18) / 2, shifted left a bit
    int xs = (SCREEN_W - len * 18) / 2 - 4;
    draw_text_x3(xs, 0, buf, len);

    // "km/h" indicator on the right
    int unit_x = xs + len * 18 + 2;
    if (unit_x < 110) {
        draw_char_5x7(unit_x, 2, 'k');
        draw_char_5x7(unit_x+6, 2, 'm');
        draw_char_5x7(unit_x+12, 9, '/');
        draw_char_5x7(unit_x+18, 9, 'h');
    }

    // ── Bottom line (y=24): [G] + ODO ──
    char line[32];
    int pos = 0;
    pos += snprintf(line + pos, sizeof(line) - pos, "[%c] ", data.gear);
    if (data.odometer_km > 0)
        pos += snprintf(line + pos, sizeof(line) - pos, "%u km", (unsigned int)data.odometer_km);
    else
        pos += snprintf(line + pos, sizeof(line) - pos, "--- km");
    draw_text(0, 24, line, pos);

    // ── Status icons (top-right) ──
    if (data.ble_connected)  draw_text(110, 0, "BT", 2);
    if (data.vehicle_awake)  draw_text(110, 9, "ON", 2);
    if (!data.valid)         draw_text(70, 24, "NO DATA", 7);

    flush();

    last_data_ = data;
    last_connected_ = data.ble_connected;
    first_render_ = false;
}
