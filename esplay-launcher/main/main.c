// Esplay Launcher - launcher for ESPLAY based on Gogo Launcher for Odroid Go.

#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"

#include "settings.h"
#include "gamepad.h"
#include "display.h"
#include "audio.h"
#include "power.h"
#include "sdcard.h"
#include "esplay-ui.h"
#include "ugui.h"
#include "gfxTile.h"

#define SCROLLSPD 16
#define NUM_EMULATOR 6

char emu_dir[10][6] = {"nes", "gb", "gbc", "sms", "gg", "col"};
int emu_slot[6] = {1, 2, 2, 3, 3, 3};
char *base_path = "/sd/roms/";
extern uint16_t fb[];
battery_state bat_state;
int num_menu = 5;
char menu_text[5][20] = {"WiFi AP *", "Volume", "Brightness", "Upscaler", "Quit"};
char scaling_text[3][20] = {"Native", "Normal", "Stretch"};
uint8_t wifi_en;

esp_err_t start_file_server(const char *base_path);

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    return ESP_OK;
}

static void renderGraphics(int dx, int dy, int sx, int sy, int sw, int sh)
{
    renderGfx(dx, dy, sw, sh, gfxTile.pixel_data, sx, sy, gfxTile.width);
}

static void scrollGfx(int dx, int dy, int sx, int sy, int sw, int sh)
{
    if (dx < 0)
    {
        sx -= dx;
        sw += dx;
        dx = 0;
    }
    if ((dx + sw) > 320)
    {
        sw -= ((dx + sw) - 320);
        dx = 320 - sw;
    }
    if (dy < 0)
    {
        sy -= dy;
        sh += dy;
        dy = 0;
    }
    if ((dy + sh) > 240)
    {
        sh -= ((dy + sh) - 240);
        dy = 240 - sh;
    }
    renderGraphics(dx, dy, sx, sy, sw, sh);
}

static void drawBattery(int batPercent)
{
    charging_state st = getChargeStatus();
    if (st == CHARGING)
        renderGraphics(320 - 25, 0, 24 * 5, 0, 24, 24);
    else if (st == FULL_CHARGED)
        renderGraphics(320 - 25, 0, 24 * 6, 0, 24, 24);
    else
    {
        if (batPercent > 75 && batPercent <= 100)
            renderGraphics(320 - 25, 0, 24 * 4, 0, 24, 24);
        else if (batPercent > 50 && batPercent <= 75)
            renderGraphics(320 - 25, 0, 24 * 3, 0, 24, 24);
        else if (batPercent > 25 && batPercent <= 50)
            renderGraphics(320 - 25, 0, 24 * 2, 0, 24, 24);
        else if (batPercent > 0 && batPercent <= 25)
            renderGraphics(320 - 25, 0, 24 * 1, 0, 24, 24);
        else if (batPercent == 0)
            renderGraphics(320 - 25, 0, 0, 0, 24, 24);
    }
}

static void drawVolume(int volume)
{
    if (volume == 0)
        renderGraphics(0, 0, 24 * 9, 0, 24, 24);
    else
        renderGraphics(0, 0, 24 * 8, 0, 24, 24);
}

static void drawHomeScreen()
{
    UG_SetForecolor(C_YELLOW);
    UG_SetBackcolor(C_BLACK);
    char *title = "ESPlay Micro";
    UG_PutString((320 / 2) - (strlen(title) * 9 / 2), 12, title);

    if (wifi_en)
    {
        title = "Wifi ON, go to http://192.168.4.1/";
        UG_PutString((320 / 2) - (strlen(title) * 9 / 2), 32, title);
    }

    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLACK);
    UG_PutString(40, 50 + (56 * 2) + 13, "    Browse");
    UG_PutString(40, 50 + (56 * 2) + 13 + 18, "  Select");
    UG_PutString(160, 50 + (56 * 2) + 13, "  Resume");
    UG_PutString(160, 50 + (56 * 2) + 13 + 18, "     Options");

    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_FillRoundFrame(35, 50 + (56 * 2) + 13 - 1, 48 + (2 * 9) + 3, 50 + (56 * 2) + 13 + 11, 7, C_WHITE);
    UG_PutString(40, 50 + (56 * 2) + 13, "< >");

    UG_FillCircle(43, 50 + (56 * 2) + 13 + 18 + 5, 7, C_WHITE);
    UG_PutString(40, 50 + (56 * 2) + 13 + 18, "A");

    UG_FillCircle(163, 50 + (56 * 2) + 13 + 5, 7, C_WHITE);
    UG_PutString(160, 50 + (56 * 2) + 13, "B");

    UG_FillRoundFrame(155, 50 + (56 * 2) + 13 + 18 - 1, 168 + (3 * 9) + 3, 50 + (56 * 2) + 13 + 18 + 11, 7, C_WHITE);
    UG_PutString(160, 50 + (56 * 2) + 13 + 18, "MENU");

    uint8_t volume = get_volume_settings();
    char volStr[3];
    sprintf(volStr, "%i", volume);
    if (volume == 0)
    {
        UG_SetForecolor(C_RED);
        UG_SetBackcolor(C_BLACK);
    }
    else
    {
        UG_SetForecolor(C_WHITE);
        UG_SetBackcolor(C_BLACK);
    }
    UG_PutString(25, 12, volStr);
    ui_flush();
    battery_level_read(&bat_state);
    drawVolume(volume);
    drawBattery(bat_state.percentage);
    if (wifi_en)
        renderGraphics(320 - (50), 0, 24 * 7, 0, 24, 24);
}

// Return to last emulator if 'B' pressed....
static int resume(void)
{
    int i;
    char *extension;
    char *romPath;

    printf("trying to resume...\n");
    romPath = get_rom_name_settings();
    if (romPath)
    {
        extension = system_util_GetFileExtenstion(romPath);
        for (i = 0; i < strlen(extension); i++)
            extension[i] = extension[i + 1];
        printf("extension=%s\n", extension);
    }
    else
    {
        printf("can't resume!\n");
        return (0);
    }
    for (i = 0; i < NUM_EMULATOR; i++)
    {
        if (strcmp(extension, &emu_dir[i][0]) == 0)
        {
            printf("resume - extension=%s, slot=%i\n", extension, i);
            system_application_set(emu_slot[i]); // set emulator slot
            ui_clear_screen();
            ui_flush();
            display_show_hourglass();
            esp_restart(); // reboot!
        }
    }
    free(romPath);
    free(extension);

    return (0); // nope!
}

static void showOptionPage(int selected)
{
    /* Header */
    UG_FillFrame(0, 0, 320 - 1, 16 - 1, C_BLUE);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLUE);
    char *msg = "Device Options";
    UG_PutString((320 / 2) - (strlen(msg) * 9 / 2), 2, msg);
    /* End Header */

    /* Footer */
    UG_FillFrame(0, 240 - 16 - 1, 320 - 1, 240 - 1, C_BLUE);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLUE);
    msg = "     Browse      Change       ";
    UG_PutString((320 / 2) - (strlen(msg) * 9 / 2), 240 - 15, msg);

    UG_FillRoundFrame(15, 240 - 15 - 1, 15 + (5 * 9) + 8, 237, 7, C_WHITE);
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_PutString(20, 240 - 15, "Up/Dn");

    UG_FillRoundFrame(140, 240 - 15 - 1, 140 + (3 * 9) + 8, 237, 7, C_WHITE);
    UG_PutString(145, 240 - 15, "< >");
    /* End Footer */

    UG_FillFrame(0, 16, 320 - 1, 240 - 20, C_BLACK);

    UG_SetForecolor(C_RED);
    UG_SetBackcolor(C_BLACK);
    UG_PutString(0, 240 - 30, "* restart required");
    char tempString[512];
    sprintf(tempString,"BuildVer: %s-%s", COMPILEDATE, GITREV);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLACK);
    UG_PutString(0, 240 - 44, tempString);
    UG_PutString(0, 240 - 58, "Retro Launcher");
    uint8_t wifi = get_wifi_settings();
    uint8_t volume = get_volume_settings();
    uint8_t bright = get_backlight_settings();
    uint8_t scaling = get_scale_option_settings();

    for (int i = 0; i < num_menu; i++)
    {
        short top = 18 + i * 15 + 8;
        if (i == selected)
        {
            UG_FillFrame(0, top - 1, 319, top + 13, C_YELLOW);
            UG_SetForecolor(C_BLACK);
            UG_SetBackcolor(C_YELLOW);
        }
        else
        {
            UG_SetForecolor(C_WHITE);
            UG_SetBackcolor(C_BLACK);
        }
        UG_PutString(0, top, menu_text[i]);

        char *val;
        // show value on right side
        switch (i)
        {
        case 0:
            val = (wifi) ? "ON" : "OFF";
            UG_PutString(319 - (strlen(val) * 9), top, val);
            break;
        case 1:
            if (i == selected)
                ui_display_progress((320 - 100 - 3), top + 2, 100, 8, (volume * 100) / 255, C_BLACK, C_YELLOW, C_BLACK);
            else
                ui_display_progress((320 - 100 - 3), top + 2, 100, 8, (volume * 100) / 255, C_WHITE, C_BLACK, C_WHITE);
            break;
        case 2:
            if (i == selected)
                ui_display_progress((320 - 100 - 3), top + 2, 100, 8, (bright * 100) / 100, C_BLACK, C_YELLOW, C_BLACK);
            else
                ui_display_progress((320 - 100 - 3), top + 2, 100, 8, (bright * 100) / 100, C_WHITE, C_BLACK, C_WHITE);
            break;
        case 3:
            UG_PutString(319 - (strlen(scaling_text[scaling]) * 9), top, scaling_text[scaling]);
            break;

        default:
            break;
        }
    }
    ui_flush();
}

static int showOption()
{
    int initial_wifi_settings = get_wifi_settings();
    int selected = 0;
    ui_clear_screen();
    showOptionPage(selected);

    input_gamepad_state prevKey;
    gamepad_read(&prevKey);

    while (true)
    {
        input_gamepad_state key;
        gamepad_read(&key);
        if (!prevKey.values[GAMEPAD_INPUT_DOWN] && key.values[GAMEPAD_INPUT_DOWN])
        {
            ++selected;
            if (selected > num_menu - 1)
                selected = 0;
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_UP] && key.values[GAMEPAD_INPUT_UP])
        {
            --selected;
            if (selected < 0)
                selected = num_menu - 1;
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_LEFT] && key.values[GAMEPAD_INPUT_LEFT])
        {
            int v = 0;
            switch (selected)
            {
            case 0:
                if (get_wifi_settings())
                    set_wifi_settings(0);
                else
                    set_wifi_settings(1);
                break;
            case 1:
                v = get_volume_settings();
                v -= 5;
                if (v < 0)
                    v = 0;
                set_volume_settings(v);
                break;
            case 2:
                v = get_backlight_settings();
                v -= 5;
                if (v < 1)
                    v = 1;
                set_backlight_settings(v);
                set_display_brightness(v);
                break;
            case 3:
                v = get_scale_option_settings();
                v--;
                if (v < 0)
                    v = 2;
                set_scale_option_settings(v);
                break;

            default:
                break;
            }
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_RIGHT] && key.values[GAMEPAD_INPUT_RIGHT])
        {
            int v = 0;
            switch (selected)
            {
            case 0:
                if (get_wifi_settings())
                    set_wifi_settings(0);
                else
                    set_wifi_settings(1);
                break;
            case 1:
                v = get_volume_settings();
                v += 5;
                if (v > 255)
                    v = 255;
                set_volume_settings(v);
                break;
            case 2:
                v = get_backlight_settings();
                v += 5;
                if (v > 100)
                    v = 100;
                set_backlight_settings(v);
                set_display_brightness(v);
                break;
            case 3:
                v = get_scale_option_settings();
                v++;
                if (v > 2)
                    v = 0;
                set_scale_option_settings(v);
                break;

            default:
                break;
            }
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_A] && key.values[GAMEPAD_INPUT_A])
            if (selected == 4)
            {
                vTaskDelay(10);
                break;
            }

        prevKey = key;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (initial_wifi_settings != get_wifi_settings())
        return 1;

    return 0;
}

//----------------------------------------------------------------
void app_main(void)
{
    nvs_flash_init();
    esplay_system_init();

    gamepad_init();

    // Display
    display_prepare();
    display_init();

    set_display_brightness((int)get_backlight_settings());

    battery_level_init();

    battery_level_read(&bat_state);
    if (bat_state.percentage == 0)
    {
        display_show_empty_battery();

        printf("PowerDown: Powerdown LCD panel.\n");
        display_poweroff();

        printf("PowerDown: Entering deep sleep.\n");
        system_sleep();

        // Should never reach here
        abort();
    }

    if (esp_reset_reason() == ESP_RST_POWERON)
        display_show_splash();

    sdcard_open("/sd"); // map SD card.

    ui_init();
    wifi_en = get_wifi_settings();

    if (wifi_en)
    {
        tcpip_adapter_init();
        ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
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

        printf("\nReady, AP on channel %d\n", (int)channel);
    }
    else
    {
        printf("\nAP Disabled, enabled wifi to use File Manager");
    }

    drawHomeScreen();
    int menuItem = 0;
    int prevItem = 0;
    int scroll = 0;
    int doRefresh = 1;
    charging_state chrg_st = getChargeStatus();
    input_gamepad_state prevKey;
    gamepad_read(&prevKey);
    while (1)
    {
        input_gamepad_state joystick;
        gamepad_read(&joystick);
        if (!prevKey.values[GAMEPAD_INPUT_LEFT] && joystick.values[GAMEPAD_INPUT_LEFT] && !scroll)
        {
            menuItem++;
            if (menuItem > NUM_EMULATOR - 1)
                menuItem = 0;
            scroll = -SCROLLSPD;
            doRefresh = 1;
        }
        if (!prevKey.values[GAMEPAD_INPUT_RIGHT] && joystick.values[GAMEPAD_INPUT_RIGHT] && !scroll)
        {
            menuItem--;
            if (menuItem < 0)
                menuItem = NUM_EMULATOR - 1;
            scroll = SCROLLSPD;
            doRefresh = 1;
        }
        if (scroll > 0)
            scroll += SCROLLSPD;
        if (scroll < 0)
            scroll -= SCROLLSPD;
        if (scroll > 320 || scroll < -320)
        {
            prevItem = menuItem;
            scroll = 0;
        }
        if (prevItem != menuItem)
            scrollGfx(scroll, 78, 0, (56 * prevItem) + 24, 320, 56);
        if (scroll)
        {
            scrollGfx(scroll + ((scroll > 0) ? -320 : 320), 78, 0, (56 * menuItem) + 24, 320, 56);
        }
        else if (doRefresh)
        {
            scrollGfx(0, 78, 0, (56 * menuItem) + 24, 320, 56);
            doRefresh = 0;
        }
        if (!prevKey.values[GAMEPAD_INPUT_A] && joystick.values[GAMEPAD_INPUT_A])
        {
            char ext[4];
            strcpy(ext, ".");
            strcat(ext, emu_dir[menuItem]);

            char *path = malloc(strlen(base_path) + strlen(emu_dir[menuItem]) + 1);
            strcpy(path, base_path);
            strcat(path, emu_dir[menuItem]);
            char *filename = ui_file_chooser(path, ext, 0, "Select Game Title");
            if (filename)
            {
                set_rom_name_settings(filename);
                system_application_set(emu_slot[menuItem]);
                ui_clear_screen();
                ui_flush();
                display_show_hourglass();
                esp_restart();
            }
            free(path);

            // B Pressed instead of A
            ui_clear_screen();
            ui_flush();
            drawHomeScreen();
            doRefresh = 1;
        }
        if (!prevKey.values[GAMEPAD_INPUT_B] && joystick.values[GAMEPAD_INPUT_B])
            resume();

        if (!prevKey.values[GAMEPAD_INPUT_MENU] && joystick.values[GAMEPAD_INPUT_MENU])
        {
            int r = showOption();
            if (r)
                esp_restart();

            ui_clear_screen();
            drawHomeScreen();
            doRefresh = 1;
        }

        if (getChargeStatus() != chrg_st)
        {
            battery_level_read(&bat_state);
            drawBattery(bat_state.percentage);
            doRefresh = 1;
            chrg_st = getChargeStatus();
        }

        prevKey = joystick;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
