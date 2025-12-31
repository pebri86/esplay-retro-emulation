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

#define ESPLAY_WIFI_SSID "ESPLAY"
#define ESPLAY_WIFI_PASS "esplay123!"
#define ESPLAY_WIFI_CHANNEL 6
#define ESPLAY_MAX_STA_CONN 2

#if CONFIG_ESP_GTK_REKEYING_ENABLE
#define ESPLAY_GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL
#else
#define ESPLAY_GTK_REKEY_INTERVAL 0
#endif

LV_IMG_DECLARE(apps);
LV_IMG_DECLARE(games);
LV_IMG_DECLARE(settings);

enum
{
    LIST_APP = 0,
    LIST_GAMES,
    LIST_SETTINGS
};

typedef enum
{
    PAGE_HOME = 0,
    PAGE_APP,
    PAGE_GAMES,
    PAGE_SETTINGS
} current_page;

typedef struct
{
    lv_group_t *input_group;
    lv_obj_t *screen;
    lv_obj_t *home_btn1;
    lv_obj_t *home_btn2;
    lv_obj_t *home_btn3;
    lv_obj_t *menu_selected_label;
    lv_obj_t *battery_label;
    lv_obj_t *time_label;
    lv_indev_t *input_device;
    current_page current_page;
} ui_state_t;

static ui_state_t ui_state = {0};
static void lv_create_homescreen();

static void lv_create_list(int type);

static void init_system_components(void);
static void init_lvgl_display(void);
static void init_ui(void);
static void run_main_loop(void);
#define LVGL_TICK_PERIOD_MS 2
#define REMOVE_FROM_GROUP 0
#define ADD_TO_GROUP 1

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESPLAY_WIFI_SSID,
            .ssid_len = strlen(ESPLAY_WIFI_SSID),
            .channel = ESPLAY_WIFI_CHANNEL,
            .password = ESPLAY_WIFI_PASS,
            .max_connection = ESPLAY_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                .required = true,
            },
#ifdef CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT
            .bss_max_idle_cfg = {
                .period = WIFI_AP_DEFAULT_MAX_IDLE_PERIOD,
                .protected_keep_alive = 1,
            },
#endif
            .gtk_rekey_interval = ESPLAY_GTK_REKEY_INTERVAL,
        },
    };
    if (strlen(ESPLAY_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             ESPLAY_WIFI_SSID, ESPLAY_WIFI_PASS, ESPLAY_WIFI_CHANNEL);
}

static void lv_keypad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    input_gamepad_state gamepad_state;
    gamepad_read(&gamepad_state);

    // Default to released
    data->state = LV_INDEV_STATE_RELEASED;

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
    // No 'else' needed here as data->state is initialized to RELEASED above
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // The px_map is passed as bytes, but for your LCD it likely
    // represents the lv_color_t array.
    lv_color_t *color_map = (lv_color_t *)px_map;

    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    lv_draw_sw_rgb565_swap(color_map, lv_area_get_size(area));

    // Send data to your LCD driver
    lcd_draw(offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);

    // IMPORTANT: In LVGL 9, use lv_display_flush_ready()
    lv_display_flush_ready(disp);
}

static void increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void kchal_set_new_app(int fd)
{
    if (fd < 0 || fd > 255)
    {
        REG_WRITE(RTC_CNTL_STORE0_REG, 0);
    }
    else
    {
        REG_WRITE(RTC_CNTL_STORE0_REG, 0xA5000000 | fd);
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

static void settings_mbox_event_cb(lv_event_t *e)
{
    lv_create_list(LIST_SETTINGS);
}

static void lv_show_battery(void)
{
    if (!ui_state.input_group)
    {
        ESP_LOGE(TAG, "Input group not initialized");
        return;
    }
    lv_group_remove_all_objs(ui_state.input_group);

    lv_obj_t *mbox1 = lv_msgbox_create(NULL);
    if (!mbox1)
    {
        ESP_LOGE(TAG, "Failed to create battery message box");
        return;
    }

    lv_msgbox_add_title(mbox1, LV_SYMBOL_BATTERY_2 " Battery");
    lv_obj_t *label = lv_msgbox_add_text(mbox1, "");
    if (!label)
    {
        lv_obj_del(mbox1);
        return;
    }

    lv_obj_t *close_btn = lv_msgbox_add_close_button(mbox1);
    if (!close_btn)
    {
        lv_obj_del(mbox1);
        return;
    }

    // Battery Logic
    battery_state bat;
    battery_level_read(&bat);
    const char *charge_status = (bat.state == CHARGING) ? "Charging" : (bat.state == FULL_CHARGED) ? "Fully Charged"
                                                                                                   : "Discharging";

    lv_label_set_text_fmt(label, "Status\n%s\nVoltage %d mV\nPercentage %d%%",
                          charge_status, bat.millivolts, bat.percentage);

    lv_obj_add_event_cb(mbox1, settings_mbox_event_cb, LV_EVENT_DELETE, NULL);
    lv_obj_center(mbox1);
    lv_group_add_obj(ui_state.input_group, close_btn);
}

static void lv_show_about(void)
{
    if (!ui_state.input_group)
    {
        ESP_LOGE(TAG, "Input group not initialized");
        return;
    }
    lv_group_remove_all_objs(ui_state.input_group);

    lv_obj_t *mbox1 = lv_msgbox_create(NULL);
    if (!mbox1)
    {
        ESP_LOGE(TAG, "Failed to create about message box");
        return;
    }
    lv_msgbox_add_title(mbox1, LV_SYMBOL_SETTINGS " About ESPlay");

    lv_obj_t *label = lv_msgbox_add_text(mbox1, "");
    if (!label)
    {
        lv_obj_del(mbox1);
        return;
    }
    lv_obj_t *close_btn = lv_msgbox_add_close_button(mbox1);
    if (!close_btn)
    {
        lv_obj_del(mbox1);
        return;
    }

    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc)
    {
        lv_label_set_text_fmt(label, "Launcher\n%s\nIDF Ver. %s\nBuilt %s %s",
                              desc->version, desc->idf_ver, desc->date, desc->time);
    }
    else
    {
        lv_label_set_text(label, "Launcher\nVersion info unavailable");
    }

    lv_obj_add_event_cb(mbox1, settings_mbox_event_cb, LV_EVENT_DELETE, NULL);
    lv_obj_center(mbox1);
    lv_group_add_obj(ui_state.input_group, close_btn);
}

static void lv_show_storage(void)
{
    if (!ui_state.input_group)
    {
        ESP_LOGE(TAG, "Input group not initialized");
        return;
    }

    uint32_t tot = 0, free_space = 0;
    sdcard_get_free_space(&tot, &free_space);

    lv_group_remove_all_objs(ui_state.input_group);
    lv_obj_t *mbox1 = lv_msgbox_create(NULL);
    if (!mbox1)
    {
        ESP_LOGE(TAG, "Failed to create storage message box");
        return;
    }
    lv_msgbox_add_title(mbox1, LV_SYMBOL_SD_CARD " Storage");

    lv_obj_t *label = lv_msgbox_add_text(mbox1, "");
    if (!label)
    {
        lv_obj_del(mbox1);
        return;
    }
    lv_obj_t *close_btn = lv_msgbox_add_close_button(mbox1);
    if (!close_btn)
    {
        lv_obj_del(mbox1);
        return;
    }

    lv_label_set_text_fmt(label, "SD Card\nTotal %lu MB\nFree %lu MB\n\nInternal Appfs\nFree %d KB",
                          (unsigned long)tot / 1024, (unsigned long)free_space / 1024, appfsGetFreeMem() / 1024);

    lv_obj_add_event_cb(mbox1, settings_mbox_event_cb, LV_EVENT_DELETE, NULL);
    lv_obj_center(mbox1);
    lv_group_add_obj(ui_state.input_group, close_btn);
}

static void list_items_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e); // This is the button in the list

    if (code == LV_EVENT_KEY)
    {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        if (key == LV_KEY_DOWN)
            lv_group_focus_next(lv_group_get_default());
        else if (key == LV_KEY_UP)
            lv_group_focus_prev(lv_group_get_default());
    }
    else if (code == LV_EVENT_CLICKED)
    {
        // v9 replacement for lv_list_get_btn_text:
        // Find the label child of the button (List buttons have an icon and a label)
        lv_obj_t *label = NULL;
        for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++)
        {
            lv_obj_t *child = lv_obj_get_child(obj, i);
            if (lv_obj_check_type(child, &lv_label_class))
            {
                label = child;
                break;
            }
        }

        if (!label)
            return;
        const char *name = lv_label_get_text(label);

        if (strcmp(name, "Back") == 0)
            lv_create_homescreen();
        else if (strcmp(name, "Storage") == 0)
            lv_show_storage();
        else if (strcmp(name, "Battery") == 0)
            lv_show_battery();
        else if (strcmp(name, "About") == 0)
            lv_show_about();
        else
        {
            // App launching logic remains the same
            int fd = appfsOpen(name);
            kchal_set_new_app(fd);
            esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
            esp_sleep_enable_timer_wakeup(10);
            esp_deep_sleep_start();
        }
    }
}

typedef int (*fc_filtercb_t)(const char *name, void *filterarg);

int filechooser_filter_glob(const char *name, void *filterarg)
{
    const char *glob = (const char *)filterarg;
    for (const char *p = glob; p != NULL; p = strchr(p, ','))
    {
        char ppart[128];
        if (*p == ',')
            p++; // skip over comma
        // Copy part of string to non-const array
        strncpy(ppart, p, sizeof(ppart));
        // Zero-terminate at ','. Make sure that worst-case it's terminated at the end of the local
        // array.
        char *pend = strchr(ppart, ',');
        if (pend != NULL)
            *pend = 0;
        else
            ppart[sizeof(ppart) - 1] = 0;
        // Try to match
        if (fnmatch(ppart, name, FNM_CASEFOLD) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int nextFdFileForFilter(int fd, fc_filtercb_t filter, void *filterarg, const char **name)
{
    while (1)
    {
        fd = appfsNextEntry(fd);
        if (fd == APPFS_INVALID_FD)
            break;
        appfsEntryInfo(fd, name, NULL);
        if (filter(*name, filterarg))
            return fd;
    }
    return APPFS_INVALID_FD;
}

/* Removed unused remove_ext function */

static void lv_add_file_list(lv_obj_t *list, void *extension)
{
    if (!list || !extension)
    {
        ESP_LOGE(TAG, "Invalid parameters for lv_add_file_list");
        return;
    }

    int fd = APPFS_INVALID_FD;
    int apps = 0;
    while (1)
    {
        const char *name = NULL;
        fd = nextFdFileForFilter(fd, filechooser_filter_glob, extension, &name);
        if (fd == APPFS_INVALID_FD || !name)
            break;
        // appfsEntryInfo is called inside nextFdFileForFilter, so name should be valid
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_FILE, name);
        if (!btn)
        {
            ESP_LOGE(TAG, "Failed to create button for %s", name);
            continue;
        }
        if (apps == 0)
            lv_group_focus_obj(btn);
        lv_obj_add_event_cb(btn, list_items_event_handler, LV_EVENT_ALL, list);
        apps++;
    }
}

typedef struct esplay_settings_t
{
    const char *icon;
    const char *text;
} esplay_settings_t;

static void lv_add_setting_list(lv_obj_t *list)
{
    esplay_settings_t set_list[] = {
        // {LV_SYMBOL_WIFI, "Wifi"},
        // {LV_SYMBOL_VOLUME_MAX, "Volume"},
        {LV_SYMBOL_SD_CARD, "Storage"},
        {LV_SYMBOL_BATTERY_2, "Battery"},
        {LV_SYMBOL_SETTINGS, "About"}};

    for (int i = 0; i < sizeof(set_list) / sizeof(set_list[0]); i++)
    {
        lv_obj_t *btn = lv_list_add_btn(list, set_list[i].icon, set_list[i].text);
        lv_obj_add_event_cb(btn, list_items_event_handler, LV_EVENT_ALL, list);
        if (i == 0)
            lv_group_focus_obj(btn);
    }
}

static void lv_populate_list_items(lv_obj_t *list, int type)
{
    switch (type)
    {
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

static void btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    if (code == LV_EVENT_KEY)
    {
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
    }
    else if (code == LV_EVENT_FOCUSED)
    {
        if (obj == ui_state.home_btn1)
            lv_label_set_text(ui_state.menu_selected_label, "Application");
        else if (obj == ui_state.home_btn2)
            lv_label_set_text(ui_state.menu_selected_label, "Games");
        else if (obj == ui_state.home_btn3)
            lv_label_set_text(ui_state.menu_selected_label, "Settings");
        else
            ESP_LOGW(TAG, "Unknown button focused");
    }
    else if (code == LV_EVENT_CLICKED)
    {
        if (obj == ui_state.home_btn1)
        {
            lv_create_list(LIST_APP);
        }
        else if (obj == ui_state.home_btn2)
        {
            lv_create_list(LIST_GAMES);
        }
        else if (obj == ui_state.home_btn3)
        {
            lv_create_list(LIST_SETTINGS);
        }
    }
}

static void lv_create_list(int type)
{
    if (!ui_state.input_group || !ui_state.screen)
    {
        ESP_LOGE(TAG, "UI state not initialized");
        return;
    }
    lv_group_remove_all_objs(ui_state.input_group);
    lv_obj_clean(ui_state.screen);

    lv_obj_t *title = lv_label_create(ui_state.screen);
    lv_obj_set_style_text_color(title, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *list = lv_list_create(ui_state.screen);
    lv_obj_set_size(list, 300, 200);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_LEFT, "Back");
    lv_obj_add_event_cb(btn, list_items_event_handler, LV_EVENT_ALL, list);

    switch (type)
    {
    case LIST_APP:
        lv_label_set_text(title, "Application");
        lv_populate_list_items(list, type);
        ui_state.current_page = PAGE_APP;
        break;
    case LIST_GAMES:
        lv_label_set_text(title, "Games");
        lv_populate_list_items(list, type);
        ui_state.current_page = PAGE_GAMES;
        break;
    case LIST_SETTINGS:
        lv_label_set_text(title, "Settings");
        lv_populate_list_items(list, type);
        ui_state.current_page = PAGE_SETTINGS;
        break;
    default:
        break;
    }
}

static void init_system_components(void)
{
    ESP_LOGI(TAG, "Initializing NVS flash");
    nvs_flash_init();

    ESP_LOGI(TAG, "Initializing battery level");
    battery_level_init();

    ESP_LOGI(TAG, "Initializing LCD display");
    lcd_init();

    ESP_LOGI(TAG, "Initializing gamepad");
    gamepad_init();

    ESP_LOGI(TAG, "Initializing AppFS");
    ESP_ERROR_CHECK(appfsInit(0x43, 3));
    ESP_LOGI(TAG, "AppFS initialized");
}

static void init_lvgl_display(void)
{
    ESP_LOGI(TAG, "Initializing LVGL library");
    lv_init();

    // Create the display object
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (!disp)
    {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return;
    }

    // Allocate and set draw buffers
    void *buf1 = heap_caps_malloc(LCD_H_RES * LINE_COUNT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(LCD_H_RES * LINE_COUNT * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1 || !buf2)
    {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        return;
    }

    lv_display_set_buffers(disp, buf1, buf2, LCD_H_RES * LINE_COUNT * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Set the flush callback
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    ESP_LOGI(TAG, "Registering input device to LVGL");
    // Create the input device object
    lv_indev_t *indev = lv_indev_create();
    if (!indev)
    {
        ESP_LOGE(TAG, "Failed to create LVGL input device");
        return;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, lv_keypad_read);
    ui_state.input_device = indev;

    ESP_LOGI(TAG, "Installing LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // Create screen
    ui_state.screen = lv_obj_create(NULL);
    lv_screen_load(ui_state.screen);

    ui_state.input_group = lv_group_create();
    lv_group_set_default(ui_state.input_group);
    lv_indev_set_group(ui_state.input_device, ui_state.input_group);

    lv_create_homescreen();
}

static void init_ui(void)
{
    sdcard_open("/sd");
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
    ESP_ERROR_CHECK(start_file_server("/sd"));
}

static void run_main_loop(void)
{
    TickType_t xLast = xTaskGetTickCount();
    while (1)
    {
        // lv_timer_handler now returns the time until the next call is needed
        uint32_t time_till_next = lv_timer_handler();

        // Dynamic delay based on LVGL needs, capped at 10ms for responsiveness
        uint32_t delay = (time_till_next > 10) ? 10 : time_till_next;
        vTaskDelay(pdMS_TO_TICKS(delay));

        TickType_t xNow = xTaskGetTickCount();
        if ((xNow - xLast) > pdMS_TO_TICKS(2000))
        {
            if (ui_state.current_page == PAGE_HOME)
            {
                char buffer[64];
                get_time(buffer, sizeof(buffer));
                lv_label_set_text(ui_state.time_label, buffer);

                battery_state bat;
                battery_level_read(&bat);

                if (bat.state == FULL_CHARGED || bat.state == CHARGING)
                    lv_label_set_text(ui_state.battery_label, LV_SYMBOL_CHARGE);
                else
                {
                    if (bat.percentage > 75)
                        lv_label_set_text(ui_state.battery_label, LV_SYMBOL_BATTERY_FULL);
                    else if (bat.percentage > 50)
                        lv_label_set_text(ui_state.battery_label, LV_SYMBOL_BATTERY_3);
                    else if (bat.percentage > 25)
                        lv_label_set_text(ui_state.battery_label, LV_SYMBOL_BATTERY_2);
                    else if (bat.percentage > 5)
                        lv_label_set_text(ui_state.battery_label, LV_SYMBOL_BATTERY_1);
                    else
                        lv_label_set_text(ui_state.battery_label, LV_SYMBOL_BATTERY_EMPTY);
                }
            }
            xLast = xNow;
        }
    }
}

static lv_obj_t *create_home_button(lv_obj_t *parent, const lv_img_dsc_t *img_src, lv_align_t align, int x_ofs, int y_ofs)
{
    lv_obj_t *btn = lv_btn_create(parent);
    if (!btn)
        return NULL;

    lv_obj_t *img = lv_img_create(btn);
    if (!img)
    {
        lv_obj_del(btn);
        return NULL;
    }
    lv_img_set_src(img, img_src);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    static lv_style_t style_def;
    static bool style_inited = false;
    if (!style_inited)
    {
        lv_style_init(&style_def);
        lv_style_set_radius(&style_def, 3);
        lv_style_set_bg_opa(&style_def, LV_OPA_20);
        lv_style_set_bg_color(&style_def, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_bg_grad_color(&style_def, lv_palette_darken(LV_PALETTE_BLUE, 2));
        lv_style_set_bg_grad_dir(&style_def, LV_GRAD_DIR_VER);
        lv_style_set_border_opa(&style_def, LV_OPA_40);
        lv_style_set_border_width(&style_def, 2);
        lv_style_set_border_color(&style_def, lv_palette_main(LV_PALETTE_GREY));
        lv_style_set_shadow_width(&style_def, 8);
        lv_style_set_shadow_color(&style_def, lv_palette_main(LV_PALETTE_GREY));
        lv_style_set_shadow_ofs_y(&style_def, 8);
        lv_style_set_outline_opa(&style_def, LV_OPA_COVER);
        lv_style_set_outline_color(&style_def, lv_palette_main(LV_PALETTE_BLUE));
        lv_style_set_text_color(&style_def, lv_color_white());
        lv_style_set_pad_all(&style_def, 10);
        style_inited = true;
    }

    lv_obj_add_style(btn, &style_def, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_YELLOW), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_grad_color(btn, lv_palette_darken(LV_PALETTE_YELLOW, 2), LV_STATE_FOCUSED);
    lv_obj_set_size(btn, 80, 80);
    lv_obj_align(btn, align, x_ofs, y_ofs);
    lv_obj_add_event_cb(btn, btn_event_handler, LV_EVENT_ALL, NULL);

    return btn;
}

static void lv_create_homescreen()
{
    if (!ui_state.input_group || !ui_state.screen)
    {
        ESP_LOGE(TAG, "UI state not initialized");
        return;
    }
    lv_group_remove_all_objs(ui_state.input_group);
    lv_obj_clean(ui_state.screen);

    char buffer[64];
    get_time(buffer, sizeof(buffer));

    lv_obj_set_style_bg_color(ui_state.screen, lv_palette_darken(LV_PALETTE_BLUE, 5), 0);
    lv_obj_set_style_bg_opa(ui_state.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(ui_state.screen, lv_palette_lighten(LV_PALETTE_BLUE, 3), 0);
    lv_obj_set_style_bg_grad_dir(ui_state.screen, LV_GRAD_DIR_VER, 0);

    ui_state.time_label = lv_label_create(ui_state.screen);
    lv_label_set_text(ui_state.time_label, buffer);
    lv_obj_set_style_text_color(ui_state.time_label, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
    lv_obj_align(ui_state.time_label, LV_ALIGN_TOP_LEFT, 5, 0);

    lv_obj_t *title = lv_label_create(ui_state.screen);
    lv_label_set_text(title, "ESPLAY 3");
    lv_obj_set_style_text_color(title, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    ui_state.battery_label = lv_label_create(ui_state.screen);
    lv_label_set_text(ui_state.battery_label, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(ui_state.battery_label, lv_palette_lighten(LV_PALETTE_GREY, 5), 0);
    lv_obj_align(ui_state.battery_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    ui_state.menu_selected_label = lv_label_create(ui_state.screen);
    lv_label_set_text(ui_state.menu_selected_label, "APPLICATION");
    lv_obj_set_style_text_color(ui_state.menu_selected_label, lv_palette_lighten(LV_PALETTE_YELLOW, 4), 0);
    lv_obj_align(ui_state.menu_selected_label, LV_ALIGN_TOP_MID, 0, 40);

    ui_state.home_btn1 = create_home_button(ui_state.screen, &apps, LV_ALIGN_LEFT_MID, 10, 0);
    ui_state.home_btn2 = create_home_button(ui_state.screen, &games, LV_ALIGN_CENTER, 0, 0);
    ui_state.home_btn3 = create_home_button(ui_state.screen, &settings, LV_ALIGN_RIGHT_MID, -10, 0);

    ui_state.current_page = PAGE_HOME;
}

void app_main(void)
{
    init_system_components();
    init_lvgl_display();
    init_ui();
    run_main_loop();
}