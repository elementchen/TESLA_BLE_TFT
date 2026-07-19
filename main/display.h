#pragma once

#include <string>
#include <vector>
#include "dash_data.h"
#include "esp_lcd_types.h"
#include "lvgl.h"
#include "ui.h"

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
    void show_pairing_status(const std::string &msg);

private:
    static constexpr const char *TAG = "Display";
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 240;

    bool initialized_ = false;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    // LVGL 刷屏回调函数
    static void my_disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);

    DashData last_data_;
};
