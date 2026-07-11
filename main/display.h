#pragma once

#include "dash_data.h"
#include <string>
#include <cstdint>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

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
    static constexpr int SCREEN_W = 240;
    static constexpr int SCREEN_H = 240;

    bool initialized_ = false;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint16_t *framebuf_ = nullptr;

    void flush();
    void fill_rect(int x, int y, int w, int h, uint16_t color);
    void draw_char_scaled(int x, int y, char c, int scale, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false);
    void draw_text_scaled(int x, int y, const char *text, int len, int scale, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false);
    
    // 特斯拉风格 UI 各功能块绘制
    void draw_status_bar(const DashData &data);
    void draw_speed(float speed);
    void draw_energy_bar(float speed);
    void draw_gears(char gear);
    void draw_odometer(uint32_t odo);

    DashData last_data_;
    bool last_connected_ = false;
    bool first_render_ = true;
};

// 5x7 字体字模声明
extern const uint8_t font5x7[][5];
