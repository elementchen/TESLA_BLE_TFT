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
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>

#include "esp_lcd_ili9341.h"
#include "ui/screens/ui_Landing.h"
#include "ui/screens/ui_Drive.h"
#include "ui/screens/ui_Charge.h"
#include "ui/screens/ui_Door_open.h"
#include "ui/screens/ui_Screen_BLE_Connect.h"
#include "ui/screens/ui_Screen_Keycard_Pair.h"
#include "ui/screens/ui_Screen_Session_Sync.h"

#define DISPLAY_SPI_SCK_PIN     12
#define DISPLAY_SPI_MOSI_PIN    11
#define DISPLAY_SPI_MISO_PIN    13
#define DISPLAY_DC_PIN          46
#define DISPLAY_SPI_CS_PIN      10
#define DISPLAY_RES             -1  // 和主控共用复位引脚
#define DISPLAY_BLK             45

// Static pointers for pairing widgets are removed as we use dedicated Screen layers.

Display::~Display() {
    if (panel_) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_) {
        esp_lcd_panel_io_del(panel_io_);
    }
    spi_bus_free(SPI3_HOST);
}

// ─── DMA 传输完毕中断通知 LVGL 回调 (异步防撕裂) ───
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

// ─── LVGL Flush 回调函数 (阴影发送缓存对换字节，完美防抗锯齿花屏) ───────────

void Display::my_disp_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    Display *display = (Display *)disp_drv->user_data;
    if (!display || !display->panel_) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    int x1 = area->x1;
    int y1 = area->y1;
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    // 分配 320x40 像素的临时硬件发送缓存，确保原 LVGL Draw Buffer (color_p) 绝不受字节翻转的物理篡改
    static uint16_t flush_buf[SCREEN_W * 40];
    int total_pixels = w * h;
    if (total_pixels <= SCREEN_W * 40) {
        uint16_t *src = (uint16_t *)color_p;
        for (int i = 0; i < total_pixels; i++) {
            uint16_t color = src[i];
            flush_buf[i] = (color >> 8) | (color << 8);
        }
        esp_lcd_panel_draw_bitmap(display->panel_, x1, y1, x1 + w, y1 + h, flush_buf);
    } else {
        // 极端溢出防御
        esp_lcd_panel_draw_bitmap(display->panel_, x1, y1, x1 + w, y1 + h, (uint16_t *)color_p);
    }
    
    // 【异步设计】：此处不再同步调用 lv_disp_flush_ready，改由 DMA 发送完毕中断自动通知
}

// ─── 驱动初始化与 LVGL 核心注册 ───────────────────────────────────────────

bool Display::init(int sda, int scl, int reset) {
    ESP_LOGI(TAG, "Initializing ILI9341 SPI LCD...");

    gpio_config_t bk_gpio_config = {};
    bk_gpio_config.mode = GPIO_MODE_OUTPUT;
    bk_gpio_config.pin_bit_mask = 1ULL << DISPLAY_BLK;
    gpio_config(&bk_gpio_config);
    gpio_set_level((gpio_num_t)DISPLAY_BLK, 1);

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
    buscfg.miso_io_num = DISPLAY_SPI_MISO_PIN;
    buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = SCREEN_W * SCREEN_H * sizeof(uint16_t);
    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        return false;
    }

    // 提前实例化静态驱动，以便将其地址绑定给 DMA 传输完成回调上下文
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
    io_config.dc_gpio_num = DISPLAY_DC_PIN;
    io_config.spi_mode = 0; // ILI9341 SPI MODE 0
    io_config.pclk_hz = 40 * 1000 * 1000; // ILI9341 SPI 降至 40MHz 确保极致电气传输稳定
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = notify_lvgl_flush_ready; // 注册 DMA 中断回调
    io_config.user_ctx = &disp_drv; // 将驱动指针传入回调上下文
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ret = esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_);
    if (ret != ESP_OK) {
        return false;
    }

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISPLAY_RES;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR; // ILI9341 BGR 色序
    panel_config.bits_per_pixel = 16;
    ret = esp_lcd_new_panel_ili9341(panel_io_, &panel_config, &panel_);
    if (ret != ESP_OK) {
        return false;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, false, false)); // 纠正左右镜像
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));   // 纠正反相颜色
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    // ─── 注册并初始化 LVGL v8 图形引擎 ───
    lv_init();

    // 内部高速 SRAM 缓冲区，大小为 320x40，流式输出效率极高且不占用太多RAM
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[SCREEN_W * 40];
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_W * 40);

    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = my_disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = this;
    lv_disp_drv_register(&disp_drv);

    // 一键构建并加载 SquareLine UI 模块
    ui_init();

    // 还原进度条显示：高保真设为设计稿原装 2 像素高度，彻底清除边框、外轮廓、阴影和内边距，保持极致清爽
    if (ui_Power_Save_Bar) {
        lv_obj_set_height(ui_Power_Save_Bar, 2);
        
        lv_obj_set_style_radius(ui_Power_Save_Bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ui_Power_Save_Bar, 0, LV_PART_INDICATOR);
        
        lv_obj_set_style_border_width(ui_Power_Save_Bar, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(ui_Power_Save_Bar, 0, LV_PART_INDICATOR);
        
        lv_obj_set_style_outline_width(ui_Power_Save_Bar, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(ui_Power_Save_Bar, 0, LV_PART_INDICATOR);
        
        lv_obj_set_style_shadow_width(ui_Power_Save_Bar, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(ui_Power_Save_Bar, 0, LV_PART_INDICATOR);
        
        lv_obj_set_style_pad_all(ui_Power_Save_Bar, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(ui_Power_Save_Bar, 0, LV_PART_INDICATOR);

        // 强行把进度条挪至 Z 轴最顶层，防止被时速等其他同级控件的半透明区域遮挡
        lv_obj_move_foreground(ui_Power_Save_Bar);
    }

    // 强力硬裁剪时速大字体的包围盒物理越界：限制时速容器所有子对象超出边界的渲染，封杀下沿像素渗漏
    if (ui_Speed) {
        lv_obj_set_style_clip_corner(ui_Speed, true, 0);
    }

    initialized_ = true;
    return true;
}

// 将整数格式化并防御性写入 Label，避免无变动时频繁 realloc 导致内存碎片化
static void set_label_int(lv_obj_t *label, int val, const char *prefix = "", const char *suffix = "") {
    if (!label) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s%d%s", prefix, val, suffix);
    const char *current_txt = lv_label_get_text(label);
    if (current_txt && strcmp(current_txt, buf) == 0) {
        return; // 无变动，拦截！
    }
    lv_label_set_text(label, buf);
}

// 将 float 格式化为一位小数并防御性写入 Label，消除无变动时的重绘与碎片化
static void set_label_float(lv_obj_t *label, float val, const char *prefix = "", const char *suffix = "") {
    if (!label) return;

    bool is_neg = (val < 0);
    float abs_val = is_neg ? -val : val;

    int int_part = (int)abs_val;
    int dec_part = (int)((abs_val - int_part) * 10.0f + 0.5f);
    if (dec_part >= 10) {
        int_part += 1;
        dec_part = 0;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%s%s%d.%d%s", prefix, is_neg ? "-" : "", int_part, dec_part, suffix);

    const char *current_txt = lv_label_get_text(label);
    if (current_txt && strcmp(current_txt, buf) == 0) {
        return; // 无变动，拦截！
    }

    lv_label_set_text(label, buf);
}

// 安全包装胎压值写入，支持高于 3.2 或低于 2.6 bar 时的橘红色 (FF5500) 变色预警并带样式差分拦截
static void set_tire_pressure(lv_obj_t *label, float val) {
    if (!label) return;
    set_label_float(label, val);
    
    lv_color_t target_color = (val < 2.6f || val > 3.2f) ? lv_color_hex(0xFF5500) : lv_color_white();
    
    // 如果颜色未改变，跳过样式重新加载以防内存开销
    lv_color_t cur_color = lv_obj_get_style_text_color(label, LV_PART_MAIN);
    if (cur_color.full != target_color.full) {
        lv_obj_set_style_text_color(label, target_color, 0);
    }
}

// ─── 统一界面真实数据渲染与数据绑定入口 ─────────────────────────────────────────

static void set_label_color_if_diff(lv_obj_t *label, lv_color_t target_color) {
    if (!label) return;
    lv_color_t cur_color = lv_obj_get_style_text_color(label, LV_PART_MAIN);
    if (cur_color.full != target_color.full) {
        lv_obj_set_style_text_color(label, target_color, 0);
    }
}

static void update_cargear_widget(lv_obj_t *cargear_obj, char gear, int battery_level, bool ble_connected) {
    if (!cargear_obj) return;
    
    // 获取组件里的子控件节点
    lv_obj_t *lbl_P = ui_comp_get_child(cargear_obj, UI_COMP_CARGEAR_LABEL_P);
    lv_obj_t *lbl_R = ui_comp_get_child(cargear_obj, UI_COMP_CARGEAR_LABEL_R);
    lv_obj_t *lbl_N = ui_comp_get_child(cargear_obj, UI_COMP_CARGEAR_LABEL_N);
    lv_obj_t *lbl_D = ui_comp_get_child(cargear_obj, UI_COMP_CARGEAR_LABEL_D);
    lv_obj_t *lbl_S = ui_comp_get_child(cargear_obj, UI_COMP_CARGEAR_LABEL_S);
    
    lv_obj_t *bar = ui_comp_get_child(cargear_obj, UI_COMP_CARGEAR_POWER_BAR);
    lv_obj_t *lbl_percent = ui_comp_get_child(cargear_obj, UI_COMP_CARGEAR_POWER_PERCENT);
    lv_obj_t *ble_dot = ui_comp_get_child(cargear_obj, UI_COMP_CARGEAR_BLE_STAUS);
    
    // 档位高亮与颜色切换，全属性差分过滤
    set_label_color_if_diff(lbl_P, (gear == 'P') ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x303030));
    set_label_color_if_diff(lbl_R, (gear == 'R') ? lv_color_hex(0xFF2A2A) : lv_color_hex(0x303030));
    set_label_color_if_diff(lbl_N, (gear == 'N') ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x303030));
    set_label_color_if_diff(lbl_D, (gear == 'D' || gear == '?') ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x303030));
    set_label_color_if_diff(lbl_S, (gear == 'S') ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x303030));
    
    // 电池进度条和电量数字防御性刷新
    if (bar) {
        if (lv_bar_get_value(bar) != battery_level) {
            lv_bar_set_value(bar, battery_level, LV_ANIM_OFF);
        }
    }
    set_label_int(lbl_percent, battery_level, "", "%");

    // 蓝牙连接状态圆点颜色实时驱动 (连接为蓝色 0x0091FF，断开为红色 0xFF0000)
    if (ble_dot) {
        lv_color_t target_dot_color = ble_connected ? lv_color_hex(0x0091FF) : lv_color_hex(0xFF0000);
        lv_color_t cur_dot_color = lv_obj_get_style_bg_color(ble_dot, LV_PART_MAIN);
        if (cur_dot_color.full != target_dot_color.full) {
            lv_obj_set_style_bg_color(ble_dot, target_dot_color, LV_PART_MAIN);
        }
    }
}

// 记录档位切换状态，用于中间时速大字的 1 秒档位特写 (提到全局以被 force_refresh 感知)
static char last_gear = '\0';
static int gear_flash_frames = 0;

void Display::render_dashboard(const DashData &data) {
    if (!initialized_) return;

    // 强制跳转安全栅栏：如果在开始更新仪表遥测时，我们还停留在配对连接子屏，则立刻强制切入主驾驶屏
    lv_obj_t *active_scr = lv_scr_act();
    if (active_scr == ui_Screen_BLE_Connect || 
        active_scr == ui_Screen_Keycard_Pair || 
        active_scr == ui_Screen_Session_Sync) {
        lv_scr_load(ui_Drive);
        lv_obj_invalidate(lv_scr_act());
    }

    // 档位变动检测与 20 帧特写重置 (约合 1 秒)
    if (data.gear != last_gear) {
        last_gear = data.gear;
        gear_flash_frames = 20;
    }

    // 计算是否处于特写帧，强行放行刷新以确保重绘到数字
    bool force_refresh = (gear_flash_frames > 0);
    static bool was_in_gear_flash = false;
    if (force_refresh || was_in_gear_flash) {
        force_refresh = true;
    }
    was_in_gear_flash = (gear_flash_frames > 0);

    // 递减特写帧计数器
    if (gear_flash_frames > 0) {
        gear_flash_frames--;
    }

    // 1. 最上游数据变动差分拦截过滤（如果状态全无更新，拦截以防止任何 CPU 开销）
    if (!force_refresh && last_data_.valid &&
        data.ble_connected == last_data_.ble_connected &&
        data.battery_level == last_data_.battery_level &&
        data.battery_range_km == last_data_.battery_range_km &&
        data.speed_kmh == last_data_.speed_kmh &&
        data.gear == last_data_.gear &&
        data.motor_power_kw == last_data_.motor_power_kw &&
        data.inside_temp == last_data_.inside_temp &&
        data.outside_temp == last_data_.outside_temp &&
        data.tpms_fl == last_data_.tpms_fl &&
        data.tpms_fr == last_data_.tpms_fr &&
        data.tpms_rl == last_data_.tpms_rl &&
        data.tpms_rr == last_data_.tpms_rr &&
        data.door_open_fl == last_data_.door_open_fl &&
        data.door_open_fr == last_data_.door_open_fr &&
        data.door_open_rl == last_data_.door_open_rl &&
        data.door_open_rr == last_data_.door_open_rr &&
        data.door_open_trunk_front == last_data_.door_open_trunk_front &&
        data.door_open_trunk_rear == last_data_.door_open_trunk_rear &&
        data.charging == last_data_.charging &&
        data.charge_power_kw == last_data_.charge_power_kw) {
        return;
    }

    // 2. 页面自动路由判断逻辑
    // 当任意车门被打开，自动切换到 door open 页面；当车辆充电中，切换至 charge 页面；其余切至默认 drive 首页
    int target_screen_type = 0; // 0: Drive, 1: Door open, 2: Charge

    bool any_door_open = (data.door_open_fl || data.door_open_fr ||
                          data.door_open_rl || data.door_open_rr ||
                          data.door_open_trunk_front || data.door_open_trunk_rear);

    if (any_door_open) {
        target_screen_type = 1; // door open
    } else if (data.charging) {
        target_screen_type = 2; // charge
    } else {
        target_screen_type = 0; // drive
    }

    static int last_screen_type = -1;
    if (target_screen_type != last_screen_type) {
        // 切屏前彻底释放和清除上一轮遗留的无线循环动画，彻底消除后台计算开销与碎片化风险
        if (ui_charge_power_animation) lv_anim_del(ui_charge_power_animation, NULL);
        if (ui_Image1)                 lv_anim_del(ui_Image1, NULL);
        if (ui_Door_open_text)         lv_anim_del(ui_Door_open_text, NULL);

        last_screen_type = target_screen_type;
        // Dedicated pairing screens handle their own cleanup.

        if (target_screen_type == 1) {
            lv_scr_load(ui_Door_open);
        } else if (target_screen_type == 2) {
            lv_scr_load(ui_Charge);
        } else {
            lv_scr_load(ui_Drive);
        }
        // 强制触发 100% 区域重绘，消除切换时的瞬时重叠影
        lv_obj_invalidate(lv_scr_act());
    }

    // ─── 3. 将遥测数据源写入对应 UI Widget 控件 ───

    // 更新常规页面顶部的 Cargear 组电量与档位高亮及蓝牙连接圆点
    if (ui_Cargear)  update_cargear_widget(ui_Cargear, data.gear, data.battery_level, data.ble_connected);
    if (ui_Cargear1) update_cargear_widget(ui_Cargear1, data.gear, data.battery_level, data.ble_connected);
    if (ui_Cargear2) update_cargear_widget(ui_Cargear2, data.gear, data.battery_level, data.ble_connected);

    // (A) 行驶页面 (ui_Drive)
    if (ui_Speed_Label) {
        // 时速大字及档位颜色控制 (R档为红色 FF2A2A，SNA/? 辅助驾驶为蓝色 0071FF，其余白色) — 全面差分拦截样式更新
        lv_color_t target_speed_color = lv_color_white();
        if (data.gear == 'R') {
            target_speed_color = lv_color_hex(0xFF2A2A);
        } else if (data.gear == '?') {
            target_speed_color = lv_color_hex(0x0071FF);
        }

        lv_color_t cur_speed_color = lv_obj_get_style_text_color(ui_Speed_Label, LV_PART_MAIN);
        if (cur_speed_color.full != target_speed_color.full) {
            lv_obj_set_style_text_color(ui_Speed_Label, target_speed_color, 0);
        }

        if (gear_flash_frames > 0) { // 1 秒档位特写 (20帧)
            if (data.gear == '?') {
                const char *cur_txt = lv_label_get_text(ui_Speed_Label);
                if (!cur_txt || strcmp(cur_txt, "D") != 0) {
                    lv_label_set_text(ui_Speed_Label, "D"); // SNA 巡航挂在 D 档上，中间特写大字显示 D
                }
            } else {
                char buf[8];
                snprintf(buf, sizeof(buf), "%c", data.gear);
                const char *cur_txt = lv_label_get_text(ui_Speed_Label);
                if (!cur_txt || strcmp(cur_txt, buf) != 0) {
                    lv_label_set_text(ui_Speed_Label, buf);
                }
            }
        } else { // 正常时速显示
            int speed_val = (int)std::round(data.speed_kmh);
            set_label_int(ui_Speed_Label, speed_val);
        }
    }
    
    // FSD Icon 定速巡航 / 辅助驾驶图标的自动显隐拦截
    if (ui_FSD_icon) {
        bool should_show = (data.gear == '?' || data.gear == 'S');
        bool is_hidden = lv_obj_has_flag(ui_FSD_icon, LV_OBJ_FLAG_HIDDEN);
        if (should_show && is_hidden) {
            lv_obj_clear_flag(ui_FSD_icon, LV_OBJ_FLAG_HIDDEN);
        } else if (!should_show && !is_hidden) {
            lv_obj_add_flag(ui_FSD_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (ui_Power_Save_Bar) {
        int power_val = (int)std::round(data.motor_power_kw);
        if (power_val < -100) power_val = -100;
        if (power_val > 100) power_val = 100;

        int cur_val = lv_bar_get_value(ui_Power_Save_Bar);
        int cur_start = lv_bar_get_start_value(ui_Power_Save_Bar);

        static int last_color_mode = -1; // -1: 初始化未知, 0: 回收(绿色), 1: 消耗(红色)
        int target_mode = (power_val >= 0) ? 1 : 0;

        if (power_val >= 0) {
            if (cur_start != 0 || cur_val != power_val) {
                lv_bar_set_start_value(ui_Power_Save_Bar, 0, LV_ANIM_OFF);
                lv_bar_set_value(ui_Power_Save_Bar, power_val, LV_ANIM_OFF);
            }
            if (last_color_mode != target_mode) {
                last_color_mode = target_mode;
                lv_obj_set_style_bg_color(ui_Power_Save_Bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR | LV_STATE_DEFAULT); // 能耗指示为红色
            }
        } else {
            if (cur_start != power_val || cur_val != 0) {
                lv_bar_set_start_value(ui_Power_Save_Bar, power_val, LV_ANIM_OFF);
                lv_bar_set_value(ui_Power_Save_Bar, 0, LV_ANIM_OFF);
            }
            if (last_color_mode != target_mode) {
                last_color_mode = target_mode;
                lv_obj_set_style_bg_color(ui_Power_Save_Bar, lv_color_hex(0x00BA11), LV_PART_INDICATOR | LV_STATE_DEFAULT); // 动能回收指示为绿色
            }
        }
    }

    // 安全避开 %f 打印机理以显示数值，并支持胎压区间预警 (低于2.6或高于3.2变橘红)
    set_tire_pressure(ui_Tire_left_front, data.tpms_fl);
    set_tire_pressure(ui_Tire_right_front, data.tpms_fr);
    set_tire_pressure(ui_Tire_left_back, data.tpms_rl);
    set_tire_pressure(ui_Tire_right_back, data.tpms_rr);

    set_label_float(ui_Inside_Temp, data.inside_temp, "", " \u00B0C");
    set_label_float(ui_Outside_Temp, data.outside_temp, "", " \u00B0C");

    // (B) 充电页面 (ui_Charge)
    set_label_float(ui_Charge_Power, data.charge_power_kw);

    set_label_int(ui_Battery_range, (int)std::round(data.battery_range_km));

    // (C) 开门警示页 (ui_Door_open)
    if (ui_Door_open_text) {
        const char *cur_txt = lv_label_get_text(ui_Door_open_text);
        if (!cur_txt || strcmp(cur_txt, "DOOR OPEN") != 0) {
            lv_label_set_text(ui_Door_open_text, "DOOR OPEN");
        }
    }

    last_data_ = data;
}

void Display::show_pairing(const std::string &msg) {
    if (!initialized_) return;

    // "TAP KEYCARD ON CENTER CONSOLE" -> 切换至高保真钥匙卡片配对页
    if (ui_Screen_Keycard_Pair) {
        if (lv_scr_act() != ui_Screen_Keycard_Pair) {
            lv_scr_load(ui_Screen_Keycard_Pair);
            lv_obj_invalidate(lv_scr_act());
        }
        if (ui_Pair_Status) {
            const char *cur_txt = lv_label_get_text(ui_Pair_Status);
            if (!cur_txt || strcmp(cur_txt, msg.c_str()) != 0) {
                lv_label_set_text(ui_Pair_Status, msg.c_str());
            }
        }
    }
}

void Display::show_pairing_status(const std::string &msg) {
    if (!initialized_) return;

    if (msg.find("Syncing") != std::string::npos) {
        // "BLE connected! Syncing telemetry..." -> 切换至密钥会话同步页，并触发 loading 旋转动画
        if (ui_Screen_Session_Sync) {
            if (lv_scr_act() != ui_Screen_Session_Sync) {
                lv_scr_load(ui_Screen_Session_Sync);
                lv_obj_invalidate(lv_scr_act());
                // 启动 loading 旋转动画
                if (ui_loading_animation) {
                    loading_Animation(ui_loading_animation, 0);
                }
            }
        }
    } else {
        // "Connecting to Vehicle (BLE)..." -> 切换至动态寻址连接页
        if (ui_Screen_BLE_Connect) {
            if (lv_scr_act() != ui_Screen_BLE_Connect) {
                lv_scr_load(ui_Screen_BLE_Connect);
                lv_obj_invalidate(lv_scr_act());
            }
            if (ui_BLE_Status_Label) {
                const char *cur_txt = lv_label_get_text(ui_BLE_Status_Label);
                if (!cur_txt || strcmp(cur_txt, msg.c_str()) != 0) {
                    lv_label_set_text(ui_BLE_Status_Label, msg.c_str());
                }
            }
        }
    }
}

void Display::clear() {}
void Display::show_splash() {}
void Display::show_error(const std::string &msg) {}
void Display::show_text_lines(const std::string &line1, const std::string &line2, const std::string &line3) {}
