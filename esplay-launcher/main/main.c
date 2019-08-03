// Esplay Launcher - launcher for ESPLAY based on Gogo Launcher for Odroid Go.

#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"

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
int e = 0, last_e = 100;
char *base_path = "/sd/roms/";
extern uint16_t fb[];
battery_state bat_state;
input_gamepad_state joystick;
int num_menu = 4;
char menu_text[5][20] = {"WiFi AP *)", "Volume", "Brightness", "Scaling"};
char scaling_text[3][20] = {"None", "Normal", "Stretch"};

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
    if (batPercent > 75 && batPercent <= 100)
        renderGraphics(320 - 25, 0, 24 * 5, 0, 24, 12);
    else if (batPercent > 50 && batPercent <= 75)
        renderGraphics(320 - 25, 0, 24 * 4, 0, 24, 12);
    else if (batPercent > 25 && batPercent <= 50)
        renderGraphics(320 - 25, 0, 24 * 3, 0, 24, 12);
    else if (batPercent > 10 && batPercent <= 25)
        renderGraphics(320 - 25, 0, 24 * 2, 0, 24, 12);
    else if (batPercent > 0 && batPercent <= 10)
        renderGraphics(320 - 25, 0, 24 * 1, 0, 24, 12);
    else if (batPercent == 0)
        renderGraphics(320 - 25, 0, 0, 0, 24, 12);
}

static void drawVolume(int volume)
{
    if (volume > 75 && volume <= 100)
        renderGraphics(0, 0, 24 * 12, 0, 24, 12);
    if (volume > 50 && volume <= 75)
        renderGraphics(0, 0, 24 * 11, 0, 24, 12);
    if (volume > 25 && volume <= 50)
        renderGraphics(0, 0, 24 * 10, 0, 24, 12);
    if (volume > 0 && volume <= 25)
        renderGraphics(0, 0, 24 * 9, 0, 24, 12);
    else if (volume == 0)
        renderGraphics(0, 0, 24 * 8, 0, 24, 12);
}

static void drawHomeScreen()
{
    UG_SetForecolor(C_YELLOW);
    char *title = "ESPlay Micro";
    UG_PutString((320 / 2) - (strlen(title) * 9 / 2), 0, title);
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
    ui_flush();
    battery_level_read(&bat_state);
    drawVolume(get_volume_settings() * 25);
    drawBattery(bat_state.percentage);
}

static void debounce(int key)
{
    while (joystick.values[key])
        gamepad_read(&joystick);
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
            display_show_hourglass();
            vTaskDelay(10);
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
    msg = "  Browse     Change    Quit";
    UG_PutString((320 / 2) - (strlen(msg) * 9 / 2), 240 - 15, msg);

    UG_FillCircle(22, 240 - 10, 7, C_WHITE);
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_PutString(20, 240 - 15, "Up Dn");

    UG_FillCircle(95, 240 - 10, 7, C_WHITE);
    UG_PutString(92, 240 - 15, "< >");

    UG_FillCircle(168, 240 - 10, 7, C_WHITE);
    UG_PutString(165, 240 - 15, "B");
    /* End Footer */

    UG_SetForecolor(C_RED);
    UG_SetBackcolor(C_BLACK);
    UG_PutString(0, 240 - 16 - 14, "*) restart required");
    uint8_t wifi = get_wifi_settings();
    uint8_t volume = get_volume_settings();
    uint8_t bright = get_backlight_settings();
    uint8_t scaling = get_scale_option_settings();

    for (int i = 0; i < num_menu; i++)
    {
        short top = 18 + i * 13 + 2;
        if (i == selected)
        {
            UG_FillFrame(0, top - 1, 320, top + 13, C_YELLOW);
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
                ui_display_progress((320 - 100 - 1), top, 100, 12, (volume / 4) * 100.0f, C_BLACK, C_YELLOW, C_BLACK);
            else
                ui_display_progress((320 - 100 - 1), top, 100, 12, (volume / 4) * 100.0f, C_WHITE, C_BLACK, C_WHITE);
            break;
        case 2:
            if (i == selected)
                ui_display_progress((320 - 100 - 1), top, 100, 12, (bright / 100) * 100.0f, C_BLACK, C_YELLOW, C_BLACK);
            else
                ui_display_progress((320 - 100 - 1), top, 100, 12, (bright / 100) * 100.0f, C_WHITE, C_BLACK, C_WHITE);
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
                v--;
                if (v < 0)
                    v = 0;
                break;
            case 2:
                v = get_backlight_settings();
                v -= 5;
                if (v < 0)
                    v = 0;
                break;
            case 3:
                v = get_scale_option_settings();
                v--;
                if (v < 0)
                    v = 0;
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
                v++;
                if (v > 4)
                    v = 4;
                break;
            case 2:
                v = get_backlight_settings();
                v += 5;
                if (v > 100)
                    v = 100;
                break;
            case 3:
                v = get_scale_option_settings();
                v++;
                if (v > 2)
                    v = 2;
                break;

            default:
                break;
            }
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_B] && key.values[GAMEPAD_INPUT_B])
            break;

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

    if (esp_reset_reason() == ESP_RST_POWERON)
        display_show_splash();

    sdcard_open("/sd"); // map SD card.

    ui_init();

    drawHomeScreen();

    int menuItem = 0;
    int prevItem = 0;
    int scroll = 0;
    input_gamepad_state prevKey;
    gamepad_read(&prevKey);
    while (1)
    {
        gamepad_read(&joystick);
        if (joystick.values[GAMEPAD_INPUT_LEFT] && !scroll)
        {
            menuItem++;
            if (menuItem > NUM_EMULATOR - 1)
                menuItem = 0;
            scroll = -SCROLLSPD;
            debounce(GAMEPAD_INPUT_LEFT);
        }
        if (joystick.values[GAMEPAD_INPUT_RIGHT] && !scroll)
        {
            menuItem--;
            if (menuItem < 0)
                menuItem = NUM_EMULATOR - 1;
            scroll = SCROLLSPD;
            debounce(GAMEPAD_INPUT_RIGHT);
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
            scrollGfx(scroll, 78, 0, (56 * prevItem) + 12, 320, 56);
        if (scroll)
        {
            scrollGfx(scroll + ((scroll > 0) ? -320 : 320), 78, 0, (56 * menuItem) + 12, 320, 56);
        }
        else
        {
            scrollGfx(0, 78, 0, (56 * menuItem) + 12, 320, 56);
        }
        if (joystick.values[GAMEPAD_INPUT_A])
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
                display_show_hourglass();
                vTaskDelay(10);
                esp_restart();
            }
            free(path);

            // B Pressed instead of A
            ui_clear_screen();
            ui_flush();
            drawHomeScreen();
            debounce(GAMEPAD_INPUT_A);
        }
        if (joystick.values[GAMEPAD_INPUT_B])
            resume();

        if (joystick.values[GAMEPAD_INPUT_MENU])
        {
            int r = showOption();
            if (r)
                esp_restart();

            ui_clear_screen();
            drawHomeScreen();
            debounce(GAMEPAD_INPUT_MENU);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
