/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "soc/rtc_cntl_reg.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
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
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "power.h"
#include "time.h"
#include "esp_timer.h"

static const char *TAG = "launcher";

LV_IMG_DECLARE(apps_img);
LV_IMG_DECLARE(games_img);
LV_IMG_DECLARE(settings_img);


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
}current_page;

static lv_group_t *g;
static lv_obj_t *scr;
static lv_obj_t *btn1;
static lv_obj_t *btn2;
static lv_obj_t *btn3;
static lv_obj_t *menu_selected;
static lv_obj_t *battery;
static lv_obj_t *time_text;
static lv_indev_t *my_indev;
static current_page cpage = PAGE_HOME;
static void lv_create_homescreen();
static void lv_create_list(int type);

#define LVGL_TICK_PERIOD_MS 2
#define REMOVE_FROM_GROUP 0
#define ADD_TO_GROUP 1


// Modern Event Handler for IDF 5.x
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    }
}


static void lv_keypad_read(lv_indev_drv_t * drv, lv_indev_data_t*data)
{
    input_gamepad_state gamepad_state;
    gamepad_read(&gamepad_state);

    if (gamepad_state.values[GAMEPAD_INPUT_UP] == 1)
    {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = LV_KEY_UP;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_DOWN] == 1)
    {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = LV_KEY_DOWN;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_LEFT] == 1)
    {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = LV_KEY_LEFT;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_RIGHT] == 1)
    {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = LV_KEY_RIGHT;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_B] == 1)
    {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = LV_KEY_ESC;
    }
    else if (gamepad_state.values[GAMEPAD_INPUT_A] == 1)
    {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = LV_KEY_ENTER;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = 0;
    }

}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    
    lcd_draw(offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    
    lv_disp_flush_ready(drv);
}

static void increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void kchal_set_new_app(int fd) {
	if (fd<0 || fd>255) {
		REG_WRITE(RTC_CNTL_STORE0_REG, 0);
	} else {
		REG_WRITE(RTC_CNTL_STORE0_REG, 0xA5000000|fd);
	}
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
    char *btn = lv_msgbox_get_active_btn_text(obj);
    if(strcmp(btn, "Close") == 0) {
        lv_msgbox_close(obj);
        lv_create_list(LIST_SETTINGS);
    }
}

static void lv_show_battery(void)
{
    lv_group_remove_all_objs(g);
    static const char * btns[] = {"Close",""};
    lv_obj_t * mbox1 = lv_msgbox_create(NULL, LV_SYMBOL_BATTERY_2 " Battery", "Battery", btns, false);
    lv_obj_t * label = lv_msgbox_get_text(mbox1);
    battery_state bat;
    battery_level_read(&bat);
    char *charge_status;
    switch(bat.state) {
        case NO_CHRG: charge_status = "Discharging";
        break;
        case CHARGING: charge_status = "Charging";
        break;
        case FULL_CHARGED: charge_status = "Fully Charge";
        break;
        default: charge_status = "Unknown";
        break;
    }
    lv_label_set_text_fmt(label, "Status\n"
                        "%s\n"
                        "Voltage %d mV\n"
                        "Percentage %d%%", charge_status, bat.millivolts, bat.percentage);
    lv_obj_add_event_cb(mbox1, settings_mbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox1);
}

static void lv_show_about(void)
{
    lv_group_remove_all_objs(g);
    static const char * btns[] = {"Close",""};
    lv_obj_t * mbox1 = lv_msgbox_create(NULL, LV_SYMBOL_SETTINGS " About ESPlay", "Launcher", btns, false);
    lv_obj_t * label = lv_msgbox_get_text(mbox1);
    esp_app_desc_t *desc = esp_ota_get_app_description();
    lv_label_set_text_fmt(label, "Launcher\n"
                        "%s\n"
                        "IDF Ver. %s\n"
                        "Built %s %s", desc->version, desc->idf_ver, desc->date, desc->time);
    lv_obj_add_event_cb(mbox1, settings_mbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox1);
}

static void lv_show_storage(void)
{
    uint32_t tot;
    uint32_t free;
    sdcard_get_free_space(&tot, &free);
    
    lv_group_remove_all_objs(g);
    static const char * btns[] = {"Close",""};
    lv_obj_t * mbox1 = lv_msgbox_create(NULL, LV_SYMBOL_SD_CARD " Storage", "Storage", btns, false);
    lv_obj_t * label = lv_msgbox_get_text(mbox1);
    lv_label_set_text_fmt(label, "SD Card\n"
                        "Total %5lu MB\n"
                        "Free %5lu MB\n\n"
                        "Internal Appfs\n"
                        "Free %d KB\n", (unsigned long)tot/1024, (unsigned long)free/1024, appfsGetFreeMem()/1024);
    lv_obj_add_event_cb(mbox1, settings_mbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox1);
}

static void list_items_event_handler(lv_event_t *e) 
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        switch (key)
        {
            case LV_KEY_DOWN: 
                lv_group_focus_next(lv_group_get_default());
                break;
            case LV_KEY_UP: 
                lv_group_focus_prev(lv_group_get_default());
                break;
            default:
                break;
        }
    } else if (code == LV_EVENT_CLICKED) {
        const char *name = lv_list_get_btn_text(lv_event_get_user_data(e), obj);
        if (strcmp(name, "Back") == 0) lv_create_homescreen();
        else if (strcmp(name, "Wifi") == 0) ESP_LOGI(TAG, "%s", name);
        else if (strcmp(name, "Volume") == 0) ESP_LOGI(TAG, "%s", name);
        else if (strcmp(name, "Storage") == 0) lv_show_storage();
        else if (strcmp(name, "Battery") == 0) lv_show_battery();
        else if (strcmp(name, "About") == 0) lv_show_about();
        else {
            int fd = appfsOpen(name);
            kchal_set_new_app(fd);
            esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
            esp_sleep_enable_timer_wakeup(10);
            esp_deep_sleep_start();
        }
    }
}

typedef int (*fc_filtercb_t)(const char *name, void *filterarg);

int filechooser_filter_glob(const char *name, void *filterarg) {
	const char *glob=(const char *)filterarg;
	for (const char *p=glob; p!=NULL; p=strchr(p, ',')) {
		char ppart[128];
		if (*p==',') p++; //skip over comma
		//Copy part of string to non-const array
		strncpy(ppart, p, sizeof(ppart));
		//Zero-terminate at ','. Make sure that worst-case it's terminated at the end of the local
		//array.
		char *pend=strchr(ppart, ',');
		if (pend!=NULL) *pend=0; else ppart[sizeof(ppart)-1]=0;
		//Try to match
		if (fnmatch(ppart, name, FNM_CASEFOLD)==0) {
			return 1;
		}
	}
	return 0;
}

static int nextFdFileForFilter(int fd, fc_filtercb_t filter, void *filterarg, const char **name) {
	while(1) {
		fd=appfsNextEntry(fd);
		if (fd==APPFS_INVALID_FD) break;
		appfsEntryInfo(fd, name, NULL);
		if (filter(*name, filterarg)) return fd;
	}
	return APPFS_INVALID_FD;
}

/*
static void remove_ext(char *fn) {
	int dot=-1;
	for (int i=0; i<strlen(fn); i++) {
		if (fn[i]=='.') dot=i;
	}
	if (dot!=-1) fn[dot]=0;
}
*/

static void lv_add_file_list(lv_obj_t *list, void* extention) {
    int fd=APPFS_INVALID_FD;
    int apps = 0;
    while(1) {
        const char *name;
        fd=nextFdFileForFilter(fd, filechooser_filter_glob, extention, &name);
        if (fd==APPFS_INVALID_FD) break;
        appfsEntryInfo(fd, &name, NULL);
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_FILE, name);
        if(apps == 0)
            lv_group_focus_obj(btn);
        lv_obj_add_event_cb(btn, list_items_event_handler, LV_EVENT_ALL, list);
        apps++;
    }
}

typedef struct esplay_settings_t{
    const char *icon;
    const char *text;
}esplay_settings_t;

static void lv_add_setting_list(lv_obj_t *list)
{
    esplay_settings_t set_list[5] = {
        {LV_SYMBOL_WIFI, "Wifi"},
        {LV_SYMBOL_VOLUME_MAX, "Volume"},
        {LV_SYMBOL_SD_CARD, "Storage"},
        {LV_SYMBOL_BATTERY_2, "Battery"},
        {LV_SYMBOL_SETTINGS, "About"}
    };
    
    for(int i=0; i<5;i++) {
        lv_obj_t *btn = lv_list_add_btn(list, set_list[i].icon, set_list[i].text);
        lv_obj_add_event_cb(btn, list_items_event_handler, LV_EVENT_ALL, list);
        if(i==0) lv_group_focus_obj(btn);
    }
}

static void lv_populate_list_items(lv_obj_t *list, int type)
{
    switch(type) {
        case LIST_APP:
        lv_add_file_list(list, "*.app");
        break;
        case LIST_GAMES:
        lv_add_file_list(list, "*.emu,*.game");
        break;
        case LIST_SETTINGS:
        lv_add_setting_list(list);
        break;
        default:
        break;
    }
}

static void btn_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        switch (key)
        {
            case LV_KEY_RIGHT: 
                lv_group_focus_next(lv_group_get_default());
                break;
            case LV_KEY_LEFT: 
                lv_group_focus_prev(lv_group_get_default());
                break;
            default:
                break;
        }
    } else if (code == LV_EVENT_FOCUSED) {
        if(obj == btn1)
            lv_label_set_text(menu_selected, "Application");
        else if(obj == btn2)
            lv_label_set_text(menu_selected, "Games");
        else if(obj == btn3)
            lv_label_set_text(menu_selected, "Settings");
        else
            printf("not selecting menu\n");
    } else if (code == LV_EVENT_CLICKED) {
        if (obj == btn1) {
            lv_create_list(LIST_APP);
        } else if (obj == btn2) {
            lv_create_list(LIST_GAMES);
        } else if (obj == btn3) {
            lv_create_list(LIST_SETTINGS);
        }
    }
}

static void lv_create_list(int type)
{
    lv_group_remove_all_objs(g);
    lv_obj_clean(scr);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_color(title, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 300, 200);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_LEFT, "Back");
    lv_obj_add_event_cb(btn, list_items_event_handler, LV_EVENT_ALL, list);

    switch(type) {
        case LIST_APP: 
        lv_label_set_text(title, "Application");
        lv_populate_list_items(list, type);
        cpage = PAGE_APP;
        break;
        case LIST_GAMES:
        lv_label_set_text(title, "Games");
        lv_populate_list_items(list, type);
        cpage = PAGE_GAMES;
        break;
        case LIST_SETTINGS:
        lv_label_set_text(title, "Settings");
        lv_populate_list_items(list, type);
        cpage = PAGE_SETTINGS;
        break;
        default:
        break;
    }
}

static void lv_create_homescreen()
{
    lv_group_remove_all_objs(g);
    lv_obj_clean(scr);
    
    char buffer[64];
    get_time(buffer, sizeof(buffer));
    
    lv_obj_set_style_bg_color(scr, lv_palette_darken(LV_PALETTE_BLUE, 5), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(scr, lv_palette_lighten(LV_PALETTE_BLUE, 3), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    
    time_text = lv_label_create(scr);
    lv_label_set_text(time_text, buffer);
    lv_obj_set_style_text_color(time_text, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
    lv_obj_align(time_text, LV_ALIGN_TOP_LEFT, 5, 0);
    
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESPLAY 3");
    lv_obj_set_style_text_color(title, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    battery = lv_label_create(scr);
    lv_label_set_text(battery, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(battery, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
    lv_obj_align(battery, LV_ALIGN_TOP_RIGHT, 0, 0);
    
    menu_selected = lv_label_create(scr);
    lv_label_set_text(menu_selected, "APPLICATION");
    lv_obj_set_style_text_color(menu_selected, lv_palette_lighten(LV_PALETTE_YELLOW, 4), 0);
    lv_obj_align(menu_selected, LV_ALIGN_TOP_MID, 0, 40);
    
    /*Init the style for the default state*/
    static lv_style_t style;
    lv_style_init(&style);

    lv_style_set_radius(&style, 3);
    //lv_style_set_text_font(&style, &lv_font_montserrat_28);
    lv_style_set_bg_opa(&style, LV_OPA_20);
    lv_style_set_bg_color(&style, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_bg_grad_color(&style, lv_palette_darken(LV_PALETTE_BLUE, 2));
    lv_style_set_bg_grad_dir(&style, LV_GRAD_DIR_VER);

    lv_style_set_border_opa(&style, LV_OPA_40);
    lv_style_set_border_width(&style, 2);
    lv_style_set_border_color(&style, lv_palette_main(LV_PALETTE_GREY));

    lv_style_set_shadow_width(&style, 8);
    lv_style_set_shadow_color(&style, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_shadow_ofs_y(&style, 8);

    lv_style_set_outline_opa(&style, LV_OPA_COVER);
    lv_style_set_outline_color(&style, lv_palette_main(LV_PALETTE_BLUE));

    lv_style_set_text_color(&style, lv_color_white());
    lv_style_set_pad_all(&style, 10);
    
    btn1 = lv_btn_create(scr);
    lv_obj_t *img = lv_img_create(btn1);
    lv_img_set_src(img, &apps_img);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(btn1, &style, 0);
    lv_obj_set_style_bg_opa(btn1, LV_OPA_60, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn1, lv_palette_main(LV_PALETTE_YELLOW), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_grad_color(btn1, lv_palette_darken(LV_PALETTE_YELLOW, 2), LV_STATE_FOCUSED);
    lv_obj_set_size(btn1, 80 ,80);
    lv_obj_align(btn1, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(btn1, btn_event_handler, LV_EVENT_ALL, NULL);
    
    btn2 = lv_btn_create(scr);
    img = lv_img_create(btn2);
    lv_img_set_src(img, &games_img);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(btn2, &style, 0);
    lv_obj_set_style_bg_opa(btn2, LV_OPA_60, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn2, lv_palette_main(LV_PALETTE_YELLOW), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_grad_color(btn2, lv_palette_darken(LV_PALETTE_YELLOW, 2), LV_STATE_FOCUSED);
    lv_obj_set_size(btn2, 80 ,80);
    lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn2, btn_event_handler, LV_EVENT_ALL, NULL);
    
    btn3 = lv_btn_create(scr);
    img = lv_img_create(btn3);
    lv_img_set_src(img, &settings_img);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(btn3, &style, 0);
    lv_obj_set_style_bg_opa(btn3, LV_OPA_60, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn3, lv_palette_main(LV_PALETTE_YELLOW), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_grad_color(btn3, lv_palette_darken(LV_PALETTE_YELLOW, 2), LV_STATE_FOCUSED);
    lv_obj_set_size(btn3, 80 ,80);
    lv_obj_align(btn3, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(btn3, btn_event_handler, LV_EVENT_ALL, NULL);
    
    cpage = PAGE_HOME;
}

void app_main(void)
{
    nvs_flash_init();
    battery_level_init();
    ESP_LOGI(TAG, "Initialize lcd display");
    lcd_init();
    ESP_LOGI(TAG, "Initialize gamepad");
    gamepad_init();
    ESP_ERROR_CHECK(appfsInit(0x43, 3));
	ESP_LOGI(TAG, "Appfs inited");

    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = heap_caps_malloc(LCD_H_RES * LINE_COUNT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);
    lv_color_t *buf2 = heap_caps_malloc(LCD_H_RES * LINE_COUNT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * 20);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Register input device driver to LVGL");
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = lv_keypad_read; 

    /*Register the driver in LVGL and save the created input device object*/
    my_indev = lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));
    
    scr = lv_obj_create(NULL);
    lv_scr_load(scr);
    g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(my_indev, g);
    lv_create_homescreen();

    sdcard_open("/sd");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "esplay",
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 2,
            .beacon_interval = 200}};
    uint8_t channel = 5;
    ap_config.ap.channel = channel;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Start the file server */
    ESP_ERROR_CHECK(start_file_server("/sd"));
    ESP_LOGI(TAG, "Ready, AP on channel %d", (int)channel);
    
    TickType_t xLast = xTaskGetTickCount();
    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
        
        TickType_t xNow = xTaskGetTickCount();
        if((xNow - xLast) > pdMS_TO_TICKS(2000))
        {   
            if(cpage == PAGE_HOME) {
                char buffer[64];
                get_time(buffer, sizeof(buffer));
                lv_label_set_text(time_text, buffer);
                
                battery_state bat;
                battery_level_read(&bat);
                
                if(bat.state == FULL_CHARGED || bat.state == CHARGING) 
                    lv_label_set_text(battery, LV_SYMBOL_CHARGE);
                else {
                    if(bat.percentage <= 100 && bat.percentage > 75 )
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_FULL);
                    else if(bat.percentage <= 75 && bat.percentage > 50 )
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_3);
                    else if(bat.percentage <= 50 && bat.percentage > 25 )
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_2);
                    else if(bat.percentage <= 25 && bat.percentage > 5 )
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_1);
                    else 
                        lv_label_set_text(battery, LV_SYMBOL_BATTERY_EMPTY);
                }
            }
                
            xLast = xNow;
        }
        
    }
}