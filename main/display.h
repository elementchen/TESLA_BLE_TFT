#pragma once

#include <string>
#include <vector>
#include "dash_data.h"
#include "fonts.h"
#include "esp_lcd_types.h"


class Display {
public:
    Display() = default;
    ~Display();

    bool init(int sda, int scl, int reset);
    void render_dashboard(const DashData &data);
    void show_error(const std::string &msg);
    void show_text_lines(const std::string &line1, const std::string &line2,
                         const std::string &line3);
    void clear();
    void show_splash();
    void show_pairing(const std::string &msg);

private:
    static constexpr const char *TAG = "Display";
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 240;

    bool initialized_ = false;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void fill_rect(int x, int y, int w, int h, uint16_t color);
    
    // 高清 Sans-Serif 字体绘制接口 (6x12, 16x32 与 40x80)
    void draw_char_6x12(int x, int y, char c, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false);
    void draw_text_6x12(int x, int y, const char *text, int len, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false);
    void draw_char_16x32(int x, int y, char c, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false);
    void draw_text_16x32(int x, int y, const char *text, int len, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false);
    void draw_char_40x80(int x, int y, char c, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false);
    void draw_text_40x80(int x, int y, const char *text, int len, uint16_t color, uint16_t bg = 0x0000, bool use_bg = false);
    void draw_bitmap(int x, int y, int w, int h, const uint16_t *bitmap);
    
    // 画线与几何轮廓原语 (Bresenham 算法与空心矩形)
    void draw_line(int x0, int y0, int x1, int y1, uint16_t color);
    void draw_rect_outline(int x, int y, int w, int h, uint16_t color);

    // 三大子页面绘制函数
    void draw_driving_screen(const DashData &data);
    void draw_closures_screen(const DashData &data);
    void draw_charging_screen(const DashData &data);
    
    // 极简车身/底盘线框图手绘函数
    void draw_car_chassis(int cx, int cy, const DashData &data);

    // 特斯拉风格 UI 各功能块绘制
    void draw_status_bar(const DashData &data);
    void draw_speed_or_gear(float speed, char gear);
    void draw_power_bar_dual(int x, int y, int w, int h, float power_kw);
    void draw_odometer(uint32_t odo);

    // 局部重绘所需的状态变量缓存
    DashData last_data_;
    bool is_first_render_ = true;
    char prev_drawn_speed_gear_str_[16] = {0};

    char prev_gear_ = '\0';
    int64_t gear_switch_time_ms_ = 0;
};
