#pragma once

#include "dash_data.h"
#include "fonts.h"
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
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 240;

    bool initialized_ = false;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void fill_rect(int x, int y, int w, int h, uint16_t color);
    
    // 高清 Sans-Serif 字体绘制接口 (8x16 与 24x48)
    void draw_char_8x16(int x, int y, char c, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false, int scale = 1);
    void draw_text_8x16(int x, int y, const char *text, int len, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false, int scale = 1);
    void draw_char_24x48(int x, int y, char c, uint16_t color);
    void draw_text_24x48(int x, int y, const char *text, int len, uint16_t color);
    
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
