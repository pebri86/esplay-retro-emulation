#include <stdio.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "soc/rtc_cntl_reg.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h" // New for IDF 5.x
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lcd.h"
#include "gamepad.h"
#include "sdcard.h"
#include "appfs.h"
#include "file_server.h"
#include "fnmatch.h"
#include "esp_ota_ops.h"
#include "power.h"
#include "esp_timer.h"

static const char *TAG = "launcher";

LV_IMG_DECLARE(apps_img);
LV_IMG_DECLARE(games_img);
LV_IMG_DECLARE(settings_img);

// Modern Event Handler for IDF 5.x
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

enum {
    LIST_APP = 0,
    LIST_GAMES,
    LIST_SETTINGS
};

typedef enum {
    PAGE_HOME = 0,
    PAGE_APP,
    PAGE_GAMES,
    PAGE_SETTINGS
} current_page;

static lv_group_t *g;
static lv_obj_t *scr;
static lv_obj_t *btn1, *btn2, *btn3;
static lv_obj_t *menu_selected;
static lv_obj_t *battery;
static lv_obj_t *time_text;
static lv_indev_t *my_indev;
static current_page cpage = PAGE_HOME;

static void lv_create_homescreen(void);
static void lv_create_list(int type);

#define LVGL_TICK_PERIOD_MS 2

// Updated Keypad Read for LVGL 8
static void lv_keypad_read(lv_indev_drv_t * drv, lv_indev_data_t* data)
{
    input_gamepad_state gamepad_state;
    gamepad_read(&gamepad_state);

    if (gamepad_state.values[GAMEPAD_INPUT_UP]) data->key = LV_KEY_UP;
    else if (gamepad_state.values[GAMEPAD_INPUT_DOWN]) data->key = LV_KEY_DOWN;
    else if (gamepad_state.values[GAMEPAD_INPUT_LEFT]) data->key = LV_KEY_LEFT;
    else if (gamepad_state.values[GAMEPAD_INPUT_RIGHT]) data->key = LV_KEY_RIGHT;
    else if (gamepad_state.values[GAMEPAD_INPUT_B]) data->key = LV_KEY_ESC;
    else if (gamepad_state.values[GAMEPAD_INPUT_A]) data->key = LV_KEY_ENTER;
    else data->key = 0;

    data->state = (data->key != 0) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    lcd_draw(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void get_time(char *buffer, int size)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    setenv("TZ", "UTC+8", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(buffer, size, "%H:%M", &timeinfo);
}

static void settings_mbox_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_current_target(e);
    uint16_t btn_id = lv_msgbox_get_active_btn(obj);
    if(btn_id == 0) { // "Close" button
        lv_msgbox_close(obj);
        lv_create_list(LIST_SETTINGS);
    }
}

static void lv_show_battery(void)
{
    lv_group_remove_all_objs(g);
    static const char * btns[] = {"Close", ""};
    lv_obj_t * mbox1 = lv_msgbox_create(NULL, LV_SYMBOL_BATTERY_2 " Battery", "", btns, false);
    
    battery_state bat;
    battery_level_read(&bat);
    const char *charge_status = (bat.state == CHARGING) ? "Charging" : (bat.state == FULL_CHARGED) ? "Fully Charged" : "Discharging";

    lv_obj_t * msg_text = lv_msgbox_get_text(mbox1);
    lv_label_set_text_fmt(msg_text, "Status: %s\nVoltage: %d mV\nPercentage: %d%%", charge_status, bat.millivolts, bat.percentage);
    
    lv_obj_add_event_cb(mbox1, settings_mbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox1);
}

static void list_items_event_handler(lv_event_t *e) 
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    
    if (code == LV_EVENT_CLICKED) {
        const char *name = lv_list_get_btn_text(lv_event_get_user_data(e), obj);
        if (strcmp(name, "Back") == 0) lv_create_homescreen();
        else if (strcmp(name, "Storage") == 0) /* lv_show_storage logic */;
        else if (strcmp(name, "Battery") == 0) lv_show_battery();
        else {
            // App Launching Logic
            int fd = appfsOpen(name);
            REG_WRITE(RTC_CNTL_STORE0_REG, 0xA5000000 | fd);
            esp_deep_sleep_start();
        }
    }
}

static void lv_create_homescreen()
{
    lv_group_remove_all_objs(g);
    lv_obj_clean(scr);
    
    // Background style in LVGL 8
    lv_obj_set_style_bg_color(scr, lv_palette_darken(LV_PALETTE_BLUE, 4), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

    time_text = lv_label_create(scr);
    lv_obj_align(time_text, LV_ALIGN_TOP_LEFT, 10, 5);
    
    menu_selected = lv_label_create(scr);
    lv_label_set_text(menu_selected, "APPLICATION");
    lv_obj_align(menu_selected, LV_ALIGN_TOP_MID, 0, 40);

    // Button 1: Apps
    btn1 = lv_btn_create(scr);
    lv_obj_set_size(btn1, 80, 80);
    lv_obj_align(btn1, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_t *img1 = lv_img_create(btn1);
    lv_img_set_src(img1, &apps_img);
    lv_obj_center(img1);
    
    // Adding to group for keypad navigation
    lv_group_add_obj(g, btn1);
    
    cpage = PAGE_HOME;
}

void app_main(void)
{
    // 1. Initialize System
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Networking (IDF 5.x Style)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    // 3. Initialize Display & Input
    lcd_init();
    gamepad_init();
    lv_init();

    // 4. LVGL Buffer Allocation
    lv_color_t *buf1 = heap_caps_malloc(LCD_H_RES * 20 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, LCD_H_RES * 20);

    // 5. Register Driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // 6. Register Input
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = lv_keypad_read;
    my_indev = lv_indev_drv_register(&indev_drv);

    g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(my_indev, g);

    // 7. Tick Timer
    const esp_timer_create_args_t tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer;
    esp_timer_create(&tick_timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000);

    scr = lv_scr_act();
    lv_create_homescreen();

    // 8. Main Loop
    while (1) {
        lv_timer_handler(); // LVGL 8 renaming
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}