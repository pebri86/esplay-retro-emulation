// Esplay Launcher - launcher for ESPLAY based on Gogo Launcher for Odroid Go.
#include "limits.h" /* PATH_MAX */
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "driver/gpio.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "soc/dport_reg.h"

#include "rom/rtc.h"
#include "soc/soc.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"

#include "settings.h"
#include "gamepad.h"
#include "event.h"
#include "display.h"
#include "audio.h"
#include "power.h"
#include "sdcard.h"
#include "esplay-ui.h"
#include "ugui.h"
#include "graphics.h"
#include "audio_player.h"
#include "file_ops.h"

#define SCROLLSPD 64
#define NUM_EMULATOR 7
#define AUDIO_FILE_PATH "/sd/audio"

char emu_dir[6][10] = {"nes", "gb", "gbc", "sms", "gg", "col"};
char emu_name[6][20] = {"Nintendo", "Gameboy", "Gameboy Color", "Sega Master System", "Game Gear", "Coleco Vision"};
int emu_slot[6] = {1, 2, 2, 3, 3, 3};
char *base_path = "/sd/roms/";
battery_state bat_state;
int num_menu = 6;
char menu_text[6][20] = {"WiFi AP *", "Volume", "Brightness", "Upscaler", "Scale Alg", "Quit"};
char scaling_text[3][20] = {"Native", "Normal", "Stretch"};
char scaling_alg_text[3][20] = {"Nearest Neighbor", "Bilinier Intrp.", "Box Filtered"};
int32_t wifi_en = 0;
int32_t volume = 25;
int32_t bright = 50;
int32_t scaling = SCALE_FIT;
int32_t scale_alg = NEAREST_NEIGHBOR;


esp_err_t start_file_server(const char *base_path);

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    return ESP_OK;
}

static void handleCharging() {
	int r;
	int fullCtr=0;
	//The LiIon charger sometimes goes back from 'full' to 'charging', which is
	//confusing to the end user. This variable becomes true if the LiIon has indicated 'full'
	//for a while, and it being true causes the 'full' icon to always show.
	int fixFull=0;

	//Force brightness low to decrease chance of burn-in
	set_display_brightness(30);
	printf("Detected charger.\n");
	guiCharging(0);

	//Speed down
	rtc_clk_cpu_freq_set(RTC_CPU_FREQ_2M);
    input_gamepad_state prevKey;
    gamepad_read(&prevKey);
	do {
        input_gamepad_state key;
        gamepad_read(&key);
		r=getChargeStatus();
		if (r==FULL_CHARGED || fixFull) {
			guiFull();
			printf("Full!\n");
			fullCtr++;
		} else if (r==CHARGING) {
            battery_level_read(&bat_state);
			guiCharging(bat_state.millivolts > 4100);
			printf("Charging...\n");
			fullCtr=0;
		}

		if (!prevKey.values[GAMEPAD_INPUT_A] && key.values[GAMEPAD_INPUT_A]) {
			printf("Power btn A; go to launcher menu\n");
			break;
		}
		if (fullCtr==32) {
			fixFull=1;
		}
		vTaskDelay(1);
        prevKey = key;
	} while (r!=NO_CHRG);

    rtc_clk_cpu_freq_set(RTC_CPU_FREQ_240M);
}

static void drawHomeScreen()
{
    ui_clear_screen();
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

    uint8_t volume = 25;
    settings_load(SettingAudioVolume, &volume);
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
    char *romPath = settings_load_str(SettingRomPath);

    printf("trying to resume...\n");
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
    ui_clear_screen();
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
    esp_app_desc_t *desc = esp_ota_get_app_description();
    char idfVer[512];
    sprintf(idfVer, "IDF %s", desc->idf_ver);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLACK);
    UG_PutString(0, 240 - 72, desc->project_name);
    UG_PutString(0, 240 - 58, desc->version);
    UG_PutString(0, 240 - 44, idfVer);

    for (int i = 0; i < num_menu; i++)
    {
        short top = 18 + i * 15 + 8;
        if (i == selected)
            UG_SetForecolor(C_YELLOW);
        else
            UG_SetForecolor(C_WHITE);

        UG_PutString(0, top, menu_text[i]);

        // show value on right side
        switch (i)
        {
        case 0:
            if (i == selected)
                ui_display_switch(307, top, wifi_en, C_YELLOW, C_BLUE, C_GRAY);
            else
                ui_display_switch(307, top, wifi_en, C_WHITE, C_BLUE, C_GRAY);
            break;
        case 1:
            if (i == selected)
                ui_display_seekbar((320 - 103), top + 4, 100, (volume * 100) / 100, C_YELLOW, C_RED);
            else
                ui_display_seekbar((320 - 103), top + 4, 100, (volume * 100) / 100, C_WHITE, C_RED);
            break;
        case 2:
            if (i == selected)
                ui_display_seekbar((320 - 103), top + 4, 100, (bright * 100) / 100, C_YELLOW, C_RED);
            else
                ui_display_seekbar((320 - 103), top + 4, 100, (bright * 100) / 100, C_WHITE, C_RED);
            break;
        case 3:
            UG_PutString(319 - (strlen(scaling_text[scaling]) * 9), top, scaling_text[scaling]);
            break;

        case 4:
            UG_PutString(319 - (strlen(scaling_alg_text[scale_alg]) * 9), top, scaling_alg_text[scale_alg]);
            break;

        default:
            break;
        }
    }
    ui_flush();
}

static int showOption()
{
    int32_t wifi_state = wifi_en;
    int selected = 0;
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
            switch (selected)
            {
            case 0:
                wifi_en = ! wifi_en;
                settings_save(SettingWifi, (int32_t)wifi_en);
                break;
            case 1:
                volume -= 5;
                if (volume < 0)
                    volume = 0;
                settings_save(SettingAudioVolume, (int32_t)volume);
                break;
            case 2:
                bright -= 5;
                if (bright < 1)
                    bright = 1;
                set_display_brightness(bright);
                settings_save(SettingBacklight, (int32_t)bright);
                break;
            case 3:
                scaling--;
                if (scaling < 0)
                    scaling = 2;
                settings_save(SettingScaleMode, (int32_t)scaling);
                break;
            case 4:
                scale_alg--;
                if (scale_alg < 0)
                    scale_alg = 1;
                settings_save(SettingAlg, (int32_t)scale_alg);
                break;

            default:
                break;
            }
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_RIGHT] && key.values[GAMEPAD_INPUT_RIGHT])
        {
            switch (selected)
            {
            case 0:
                wifi_en = !wifi_en;
                settings_save(SettingWifi, (int32_t)wifi_en);
                break;
            case 1:
                volume += 5;
                if (volume > 100)
                    volume = 100;
                settings_save(SettingAudioVolume, (int32_t)volume);
                break;
            case 2:
                bright += 5;
                if (bright > 100)
                    bright = 100;
                set_display_brightness(bright);
                settings_save(SettingBacklight, (int32_t)bright);
                break;
            case 3:
                scaling++;
                if (scaling > 2)
                    scaling = 0;
                settings_save(SettingScaleMode, (int32_t)scaling);
                break;
            case 4:
                scale_alg++;
                if (scale_alg > 1)
                    scale_alg = 0;
                settings_save(SettingAlg, (int32_t)scale_alg);
                break;

            default:
                break;
            }
            showOptionPage(selected);
        }
        if (!prevKey.values[GAMEPAD_INPUT_A] && key.values[GAMEPAD_INPUT_A])
            if (selected == 5)
            {
                vTaskDelay(10);
                break;
            }

        prevKey = key;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    if (wifi_en != wifi_state)
        return 1;

    return 0;
}

//----------------------------------------------------------------
void app_main(void)
{
    settings_init();
    esplay_system_init();

    audio_init(44100);
    audio_amp_disable();

    gamepad_init();

    // Display
    display_prepare();
    display_init();

    int brightness = 50;
    settings_load(SettingBacklight, &brightness);
    set_display_brightness(brightness);

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
    charging_state st = getChargeStatus();
    if (st == CHARGING)
        handleCharging();

    UG_FontSelect(&FONT_8X12);
    if(settings_load(SettingWifi, &wifi_en) != 0)
        settings_save(SettingWifi, (int32_t)wifi_en);
    if(settings_load(SettingAudioVolume, &volume) != 0)
        settings_save(SettingAudioVolume, (int32_t)volume);
    if(settings_load(SettingBacklight, &bright) != 0)
        settings_save(SettingBacklight, (int32_t)bright);
    if(settings_load(SettingScaleMode, &scaling) != 0)
        settings_save(SettingScaleMode, (int32_t)scaling);
    if(settings_load(SettingAlg, &scale_alg) != 0)
        settings_save(SettingAlg, (int32_t)scale_alg);

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
        printf("\nAP Disabled, enabled wifi to use File Manager\n");
    }

    drawHomeScreen();
    int menuItem = 0;
    int prevItem = 0;
    int scroll = 0;
    int doRefresh = 1;
    int oldArrowsTick = -1;
    int lastUpdate = 0;
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
        }
        if (!prevKey.values[GAMEPAD_INPUT_RIGHT] && joystick.values[GAMEPAD_INPUT_RIGHT] && !scroll)
        {
            menuItem--;
            if (menuItem < 0)
                menuItem = NUM_EMULATOR - 1;
            scroll = SCROLLSPD;
        }
        if (scroll > 0)
            scroll += SCROLLSPD;
        if (scroll < 0)
            scroll -= SCROLLSPD;
        if (scroll > 320 || scroll < -320)
        {
            prevItem = menuItem;
            scroll = 0;
            doRefresh = 1;
        }
        if (prevItem != menuItem)
            renderGraphics(scroll, 78, 0, (56 * prevItem) + 24, 320, 56);
        if (scroll)
        {
            renderGraphics(scroll + ((scroll > 0) ? -320 : 320), 78, 0, (56 * menuItem) + 24, 320, 56);
            doRefresh = 1;
            lastUpdate = 0;
        }
        else
        {
            int update = 1;
            if (update != lastUpdate)
            {
                if (menuItem < 6)
                {
                    char *path = malloc(strlen(base_path) + strlen(emu_dir[menuItem]) + 1);
                    strcpy(path, base_path);
                    strcat(path, emu_dir[menuItem]);
                    int count = sdcard_get_files_count(path);
                    char text[320];
                    sprintf(text, "%i games available", count);
                    UG_FillFrame(0, 64, 319, 76, C_BLACK);
                    UG_PutString((320 / 2) - (strlen(text) * 9 / 2), 64, text);
                    renderGraphics(0, 78, 0, (56 * menuItem) + 24, 320, 56);
                }
                else
                {
                    UG_FillFrame(0, 64, 319, 76, C_BLACK);
                    renderGraphics(0, 78, 0, (56 * menuItem) + 24, 320, 56);
                }

                    lastUpdate = update;

            }
            //Render arrows
            int t = xTaskGetTickCount() / (400 / portTICK_PERIOD_MS);
            t = (t & 1);
            if (t != oldArrowsTick)
            {
                doRefresh = 1;
                renderGraphics(10, 90, t ? 0 : 32, 416, 32, 23);
                renderGraphics(276, 90, t ? 64 : 96, 416, 32, 23);
                oldArrowsTick = t;
            }
        }
        if (!prevKey.values[GAMEPAD_INPUT_A] && joystick.values[GAMEPAD_INPUT_A])
        {
            if (menuItem < 6)
            {
                char ext[4];
                strcpy(ext, ".");
                strcat(ext, emu_dir[menuItem]);

                char *path = malloc(strlen(base_path) + strlen(emu_dir[menuItem]) + 1);
                strcpy(path, base_path);
                strcat(path, emu_dir[menuItem]);
                char *filename = ui_file_chooser(path, ext, 0, emu_name[menuItem]);
                if (filename)
                {
                    settings_save_str(SettingRomPath, filename);
                    system_application_set(emu_slot[menuItem]);
                    ui_clear_screen();
                    ui_flush();
                    display_show_hourglass();
                    esp_restart();
                }
                free(path);
            }
            else
            {
                Entry *new_entries;

                int n_entries = fops_list_dir(&new_entries, AUDIO_FILE_PATH);
                audio_player((AudioPlayerParam){new_entries, n_entries, 0, AUDIO_FILE_PATH, true});

                fops_free_entries(&new_entries, n_entries);

                UG_FontSelect(&FONT_8X12);
                drawHomeScreen();
                lastUpdate = 0;
                doRefresh = 1;
            }

            // B Pressed instead of A
            drawHomeScreen();
            lastUpdate = 0;
            doRefresh = 1;
        }
        if (!prevKey.values[GAMEPAD_INPUT_B] && joystick.values[GAMEPAD_INPUT_B])
            resume();

        if (!prevKey.values[GAMEPAD_INPUT_MENU] && joystick.values[GAMEPAD_INPUT_MENU])
        {
            int r = showOption();
            if (r)
                esp_restart();

            drawHomeScreen();
            lastUpdate = 0;
            doRefresh = 1;
        }

        if (getChargeStatus() != chrg_st)
        {
            battery_level_read(&bat_state);
            drawBattery(bat_state.percentage);
            doRefresh = 1;
            chrg_st = getChargeStatus();
        }

        if (doRefresh)
            ui_flush();

        doRefresh = 0;
        prevKey = joystick;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
