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

#include "font.h"
#include "img_assets.h"

#include <string.h>
#include <dirent.h>

#define SCROLLSPD 16

input_gamepad_state joystick;

battery_state bat_state;

int BatteryPercent = 100;

unsigned short buffer[40000];
int colour = 65535; // white text mostly.

int num_emulators = 9;
char emulator[10][32] = {"Nintendo", "GAMEBOY", "GAMEBOY COLOR", "SEGA MASTER SYSTEM", "GAME GEAR", "COLECO VISION", "Sound Volume", "Display Brightness", "Wifi AP & Display Scale Options"};
char emu_dir[10][20] = {"nes", "gb", "gbc", "sms", "gg", "col"};
int emu_slot[10] = {1, 2, 2, 3, 3, 3};
char brightness[10][5] = {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};
char volume[10][10] = {"Mute", "25%", "50%", "75%", "100%"};
char scale_options[10][50] = {"Disable scaling", "Screen Fit", "Stretch", "Enable Wifi", "Disable Wifi"};

char target[256] = "";
int e = 0, last_e = 100, x, y = 0, count = 0;

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    return ESP_OK;
}

static void renderGraphics(int dx, int dy, int sx, int sy, int sw, int sh)
{
    renderGfx(dx, dy, sw, sh, img_assets.pixel_data, sx, sy, img_assets.width);
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

//----------------------------------------------------------------
int read_config()
{
    FILE *fp;
    int v;

    if ((fp = fopen("/sd/esplay/data/gogo_conf.txt", "r")))
    {
        while (!feof(fp))
        {
            fscanf(fp, "%s %s %i\n", &emulator[num_emulators][0],
                   &emu_dir[num_emulators][0],
                   &emu_slot[num_emulators]);
            num_emulators++;
        }
    }

    return (0);
}

//----------------------------------------------------------------
void debounce(int key)
{
    while (joystick.values[key])
        gamepad_read(&joystick);
}

//----------------------------------------------------------------
// print a character string to screen...
void print(short x, short y, char *s)
{
    int i, n, k, a, len, idx;
    unsigned char c;
    //extern unsigned short  *buffer; // screen buffer, 16 bit pixels

    len = strlen(s);
    for (k = 0; k < len; k++)
    {
        c = s[k];
        for (i = 0; i < 8; i++)
        {
            a = font_8x8[c][i];
            for (n = 7, idx = i * len * 8 + k * 8; n >= 0; n--, idx++)
            {
                if (a % 2 == 1)
                    buffer[idx] = colour;
                else
                    buffer[idx] = 0;
                a = a >> 1;
            }
        }
    }
    write_frame_rectangleLE(x * 8, y * 8, len * 8, 8, buffer);
}

//--------------------------------------------------------------
void print_y(short x, short y, char *s) // print, but yellow text...
{
    colour = 0b1111111111100000;
    print(x, y, s);
    colour = 65535;
}

void clear_text(short y)
{
    char s[40];
    for (short i = 0; i < 40; i++)
        s[i] = ' ';
    s[39] = 0;
    print(0, y, s);
}

void scrollLeft(int start, int end, int sx, int sy, int sw, int sh)
{
    x = start;
    int _sw = sw;
    for (int j = 0; j < _sw + start; j += SCROLLSPD)
    {
        x -= SCROLLSPD;
        if (x < end)
        {
            sx -= x;
            sw += x;
            x = end;
        }
        renderGraphics(x, 50, sx, sy, sw, sh);
        write_frame_rectangleLE(x + sw, 50, SCROLLSPD, 56, buffer);
    }
}

void scrollRight(int start, int end, int sx, int sy, int sw, int sh)
{
    for (x = start; x < end; x += SCROLLSPD)
    {
        if (x + sw > end)
        {
            sw -= (x + sw) - end;
            x = end - sw;
        }
        renderGraphics(x, 50, sx, sy, sw, sh);
        write_frame_rectangleLE(x - SCROLLSPD, 50, SCROLLSPD, 56, buffer);
    }
    write_frame_rectangleLE(0, 50, 320, 56, buffer);
}
//-------------------------------------------------------------
// display details for a particular emulator....
int print_emulator(int e, int y)
{
    int i, len, dotlen;
    int sw = 150;
    int sh = 56;
    DIR *dr;
    struct dirent *de;
    char path[256] = "/sd/roms/";
    char s[40];

    if (e != last_e)
    {
        for (i = 0; i < 320 * 56; i++)
            buffer[i] = 65535;
        write_frame_rectangleLE(0, 50, 320, 56, buffer);

        short e_prev = e - 1 < 0 ? 8 : e - 1;
        short e_next = e + 1 > 8 ? 0 : e + 1;
        short x_next = e_next % 2 == 0 ? 0 : 150;
        short x_prev = e_prev % 2 == 0 ? 0 : 150;
        short sx = (e % 2) == 0 ? 0 : 150;
        short sy = 0;

        switch (e)
        {
        case 0:
            sy = 12;
            if (last_e == 8)
            {
                scrollLeft(85, 0, x_prev, 4 * 56 + 12, sw, sh);
                //scrollLeft(320, 85, sx, sy, sw, sh);
            }
            else
            {
                scrollRight(85, 320, x_next, 12, sw, sh);
                //scrollRight(0, 85, sx, sy, sw, sh);
            }
            break;
        case 1:
            sy = 12;
            if (last_e == 0)
                scrollLeft(85, 0, x_prev, 12, sw, sh);
            else
                scrollRight(85, 320, x_next, 1 * 56 + 12, sw, sh);
            break;
        case 2:
            sy = 1 * 56 + 12;
            if (last_e == 1)
                scrollLeft(85, 0, x_prev, 12, sw, sh);
            else
                scrollRight(85, 320, x_next, 1 * 56 + 12, sw, sh);
            break;
        case 3:
            sy = 1 * 56 + 12;
            if (last_e == 2)
                scrollLeft(85, 0, x_prev, 1 * 56 + 12, sw, sh);
            else
                scrollRight(85, 320, x_next, 2 * 56 + 12, sw, sh);
            break;
        case 4:
            sy = 2 * 56 + 12;
            if (last_e == 3)
                scrollLeft(85, 0, x_prev, 1 * 56 + 12, sw, sh);
            else
                scrollRight(85, 320, x_next, 2 * 56 + 12, sw, sh);
            break;
        case 5:
            sy = 2 * 56 + 12;
            if (last_e == 4)
                scrollLeft(85, 0, x_prev, 2 * 56 + 12, sw, sh);
            else
                scrollRight(85, 320, x_next, 3 * 56 + 12, sw, sh);
            break;
        case 6:
            sy = 3 * 56 + 12;
            if (last_e == 5)
                scrollLeft(85, 0, x_prev, 2 * 56 + 12, sw, sh);
            else
                scrollRight(85, 320, x_next, 3 * 56 + 12, sw, sh);
            break;
        case 7:
            sy = 3 * 56 + 12;
            if (last_e == 6)
                scrollLeft(85, 0, x_prev, 3 * 56 + 12, sw, sh);
            else
                scrollRight(85, 320, x_next, 4 * 56 + 12, sw, sh);
            break;
        case 8:
            sy = 4 * 56 + 12;
            if (last_e == 7)
                scrollLeft(85, 0, e_prev, 3 * 56 + 12, sw, sh);
            else
                scrollRight(85, 320, e_next, 12, sw, sh);
            break;
        default:
            break;
        }

        renderGraphics(85, 50, sx, sy, 150, 56);

        len = strlen(emulator[e]);
        clear_text(18);
        print((40 - len) / 2, 18, emulator[e]);

        last_e = e;
    }

    if (e < 6)
    {
        strcat(&path[strlen(path) - 1], emu_dir[e]);
        count = 0;
        if (!(dr = opendir(path)))
        {
            for (i = 20; i < 30; i++)
                clear_text(i);
            return (0);
        }

        while ((de = readdir(dr)) != NULL)
        {
            len = strlen(de->d_name);
            dotlen = strlen(emu_dir[e]);
            // only show files that have matching extension...
            if (strcmp(&de->d_name[len - dotlen], emu_dir[e]) == 0 && de->d_name[0] != '.')
            {
                //printf("file: %s\n", de->d_name);
                if (count == y)
                {
                    strcpy(target, path);
                    i = strlen(target);
                    target[i] = '/';
                    target[i + 1] = 0;
                    strcat(target, de->d_name);
                    //printf("target=%s\n", target);
                }
                // strip extension from file...
                de->d_name[len - dotlen - 1] = 0;
                if (strlen(de->d_name) > 39)
                    de->d_name[39] = 0;
                if (y / 10 == count / 10)
                { // right page?
                    for (i = 0; i < 40; i++)
                        s[i] = ' ';
                    s[i - 1] = 0;
                    for (i = 0; i < strlen(de->d_name); i++)
                        s[i] = de->d_name[i];
                    if (count == y)
                        print_y(0, (count % 10) + 20, s); // highlight
                    else
                        print(0, (count % 10) + 20, s);
                }
                count++;
            }
        }
        if (y / 10 == count / 10)
            for (i = count % 10; i < 10; i++)
                clear_text(i + 20);
        closedir(dr);
        printf("total=%i\n", count);
    }
    else if (e == 6)
    {
        count = 0;
        while (count < 5)
        {
            for (i = 0; i < 40; i++)
                s[i] = ' ';
            s[i - 1] = 0;
            for (i = 0; i < strlen(volume[count]); i++)
                s[i] = volume[count][i];
            if (count == y)
                print_y(0, (count % 10) + 20, s); // highlight
            else
                print(0, (count % 10) + 20, s);
            count++;
        }
        if (y / 10 == count / 10)
            for (i = count % 10; i < 10; i++)
                clear_text(i + 20);
    }
    else if (e == 7)
    {
        count = 0;
        while (count < 10)
        {
            for (i = 0; i < 40; i++)
                s[i] = ' ';
            s[i - 1] = 0;
            for (i = 0; i < strlen(brightness[count]); i++)
                s[i] = brightness[count][i];
            if (count == y)
                print_y(0, (count % 10) + 20, s); // highlight
            else
                print(0, (count % 10) + 20, s);
            count++;
        }
        if (y / 10 == count / 10)
            for (i = count % 10; i < 10; i++)
                clear_text(i + 20);
    }

    else if (e == 8)
    {
        count = 0;
        while (count < 10)
        {
            for (i = 0; i < 40; i++)
                s[i] = ' ';
            s[i - 1] = 0;
            for (i = 0; i < strlen(scale_options[count]); i++)
                s[i] = scale_options[count][i];
            if (count == y)
                print_y(0, (count % 10) + 20, s); // highlight
            else
                print(0, (count % 10) + 20, s);
            count++;
        }
        if (y / 10 == count / 10)
            for (i = count % 10; i < 10; i++)
                clear_text(i + 20);
    }

    return (0);
}

//----------------------------------------------------------------
// Return to last emulator if 'B' pressed....
int resume(void)
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
    for (i = 0; i < num_emulators; i++)
    {
        if (strcmp(extension, &emu_dir[i][0]) == 0)
        {
            printf("resume - extension=%s, slot=%i\n", extension, i);
            system_application_set(emu_slot[i]); // set emulator slot
            print(14, 15, "RESUMING....");
            usleep(500000);
            esp_restart(); // reboot!
        }
    }
    free(romPath);
    free(extension);

    return (0); // nope!
}

esp_err_t start_file_server(const char *base_path);

//----------------------------------------------------------------
void app_main(void)
{
    FILE *fp;
    char s[256];
    printf("esplay start.\n");

    nvs_flash_init();
    esplay_system_init();
    gamepad_init();
    audio_init(16000);

    // Display
    display_prepare();
    display_init();

    set_display_brightness((int)get_backlight_settings());

    battery_level_init();

    if (esp_reset_reason() == ESP_RST_POWERON)
    {
        display_show_splash();
    }
    else
    {
        e = get_last_emu_settings();
        y = get_last_rom_settings();
    }

    sdcard_open("/sd"); // map SD card.

    display_clear(0); // clear screen

    battery_level_read(&bat_state);
    drawVolume(get_volume_settings() * 25);
    drawBattery(bat_state.percentage);

    renderGraphics((320 - 150) / 2, 0, 150, 236, 150, 32);

    read_config();

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
    uint8_t wifi_en = get_wifi_settings();

    if (wifi_en)
    {
        char msg[34] = "Wifi ON, go to http://192.168.4.1/";
        short len = strlen(msg);
        print_y((40 - len) / 2, 4, msg);

        renderGraphics(320 - (50), 0, 24 * 6, 0, 24, 12);
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
        renderGraphics(320 - (50), 0, 24 * 7, 0, 24, 12);
        printf("\nAP Disabled, enabled wifi to use File Manager");
    }

    print_emulator(e, y);
    while (1)
    {
        gamepad_read(&joystick);
        if (joystick.values[GAMEPAD_INPUT_LEFT])
        {
            y = 0;
            e--;
            if (e < 0)
                e = (num_emulators - 1) - 1;
            print_emulator(e, y);
            debounce(GAMEPAD_INPUT_LEFT);
        }
        if (joystick.values[GAMEPAD_INPUT_RIGHT])
        {
            y = 0;
            e++;
            if (e == num_emulators - 1)
                e = 0;
            print_emulator(e, y);
            debounce(GAMEPAD_INPUT_RIGHT);
        }
        if (joystick.values[GAMEPAD_INPUT_UP])
        {
            y--;
            if (y < 0)
                y = count - 1;
            print_emulator(e, y);
            debounce(GAMEPAD_INPUT_UP);
        }
        if (joystick.values[GAMEPAD_INPUT_DOWN])
        {
            y++;
            if (y >= count)
                y = 0;
            print_emulator(e, y);
            debounce(GAMEPAD_INPUT_DOWN);
        }
        if (joystick.values[GAMEPAD_INPUT_SELECT] && y > 9)
        {
            y -= 10;
            print_emulator(e, y);
            debounce(GAMEPAD_INPUT_SELECT);
        }
        if (joystick.values[GAMEPAD_INPUT_START] && y < count - 10)
        {
            y += 10;
            print_emulator(e, y);
            debounce(GAMEPAD_INPUT_START);
        }
        if (joystick.values[GAMEPAD_INPUT_A])
        {
            if (count != 0)
            {
                if (e == 6)
                {
                    set_volume_settings(y);
                    /*
                    sprintf(s, "Volume: %i%%   ", y * 25);
                    print(0, 0, s);
                    */
                    drawVolume(y * 25);
                    print(14, 15, "Saved....");
                    vTaskDelay(20);
                    print(14, 15, "         ");
                }
                else if (e == 7)
                {
                    set_backlight_settings((y + 1) * 10);
                    set_display_brightness((y + 1) * 10);
                    print(14, 15, "Saved....");
                    vTaskDelay(20);
                    print(14, 15, "         ");
                }
                else if (e == 8)
                {
                    if (y < 3)
                    {
                        set_scale_option_settings(y);
                        print(14, 15, "Saved....");
                        vTaskDelay(20);
                        print(14, 15, "         ");
                    }
                    else if (y == 3)
                    {
                        if (!wifi_en)
                        {
                            set_wifi_settings(1);
                            print(14, 15, "restarting....");
                            vTaskDelay(100);
                            display_poweroff();
                            backlight_deinit();
                            esp_restart();
                        }
                    }
                    else
                    {
                        if (wifi_en)
                        {
                            set_wifi_settings(0);
                            print(14, 15, "restarting....");
                            vTaskDelay(100);
                            display_poweroff();
                            backlight_deinit();
                            esp_restart();
                        }
                    }
                }
                else
                {
                    // not in an empty directory...
                    set_rom_name_settings(target);
                    set_last_emu_settings(e);
                    set_last_rom_settings(y);
                    print(14, 15, "Loading....");
                    system_application_set(emu_slot[e]); // set emulator slot
                    esp_restart();                       // reboot!
                }
            }
            debounce(GAMEPAD_INPUT_A);
        }
        if (joystick.values[GAMEPAD_INPUT_B])
            resume();
    }
}
