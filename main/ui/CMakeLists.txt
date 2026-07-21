SET(SOURCES components/ui_comp_cargear.c
    components/ui_comp.c
    screens/ui_Screen_BLE_Connect.c
    screens/ui_Screen_Keycard_Pair.c
    screens/ui_Screen_Session_Sync.c
    screens/ui_Drive.c
    screens/ui_Charge.c
    screens/ui_Door_open.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_landing3_png.c
    images/ui_img_keycard1_png.c
    images/ui_img_keycard2_png.c
    images/ui_img_keycard_titlecn_png.c
    images/ui_img_sync_3_png.c
    images/ui_img_loading_png.c
    images/ui_img_fsd_png.c
    images/ui_img_car1_png.c
    images/ui_img_charger_power_png.c
    images/ui_img_tesla_charging_2_png.c
    images/ui_img_door_open_3_png.c
    fonts/ui_font_google_sans.c
    fonts/ui_font_Number.c
    fonts/ui_font_Number_small.c)

add_library(ui ${SOURCES})
