#pragma once

#include "dash_data.h"
#include "driver/i2c_master.h"
#include <string>

class Display {
public:
    Display() = default;
    ~Display();

    bool init(int sda, int scl, int reset);
    void show_splash();
    void show_pairing(const std::string &msg);
    void render_dashboard(const DashData &data);
    void show_error(const std::string &msg);
    void show_text_lines(const std::string &line1, const std::string &line2,
                         const std::string &line3);
    void clear();

private:
    static constexpr const char *TAG = "Display";
    static constexpr int SCREEN_W = 128;
    static constexpr int SCREEN_H = 32;
    static constexpr uint8_t I2C_ADDR = 0x3C;

    int i2c_port_ = 0;
    bool initialized_ = false;
    i2c_master_bus_handle_t i2c_bus_handle_ = nullptr;
    i2c_master_dev_handle_t i2c_dev_handle_ = nullptr;
    uint8_t framebuf_[128 * 4] = {};  // 128x32 / 8 pages

    void i2c_write_cmd(uint8_t cmd);
    void i2c_write_data(const uint8_t *data, size_t len);
    void flush();
    void draw_char_5x7(int x, int y, char c, bool invert = false);
    void draw_char_15x21(int x, int y, char c);  // 3x scaled
    void draw_text(int x, int y, const char *text, int len, bool invert = false);
    void draw_text_x3(int x, int y, const char *text, int len);

    DashData last_data_;
    bool last_connected_ = false;
    bool first_render_ = true;
};

// 5x7 font table (ASCII 0x20-0x7E)
extern const uint8_t font5x7[][5];
