#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "display.h"
#include "menu_bg.h"
#include "menu_gfx.h"
#include "audio.h"
#include "gamepad.h"
#include "settings.h"
#include "menu.h"

void drawBackground(int offset)
{
  renderGfx((320 - 150) / 2, offset, 150, 240 - offset - 1, menu_bg.pixel_data, 0, offset, menu_bg.width);
}

void renderMenu(int dx, int dy, int sx, int sy, int sw, int sh)
{
  renderGfx(dx, dy, sw, sh, menu_gfx.pixel_data, sx, sy, menu_gfx.width);
}

#define SCN_VOLUME 0
#define SCN_BRIGHT 1
#define SCN_SAVE 2
#define SCN_LOAD 3
#define SCN_RESET 4
#define SCN_SAVE_EXIT 5
#define SCN_EXIT 6
#define SCN_CONTINUE 7

#define SCN_COUNT 8
int showMenu()
{
  esp_task_wdt_feed();
  input_gamepad_state lastKey;
  gamepad_read(&lastKey);
  int menuItem = 0;
  int prevItem = 0;
  int doRefresh = 1;
  int oldArrowsTick = -1;
  int refreshArrow = 0;
  int top = 0;
  int vol = 0;
  int oldVol = 0;
  int br = 0;
  int oldBr = 0;

  audio_terminate();
  while (1)
  {
    esp_task_wdt_feed();
    input_gamepad_state key;
    gamepad_read(&key);

    if (doRefresh)
      drawBackground(top);

    if (!lastKey.values[GAMEPAD_INPUT_DOWN] && key.values[GAMEPAD_INPUT_DOWN])
    {
      menuItem++;
      if (menuItem >= SCN_COUNT)
        menuItem = 0;
      refreshArrow = 1;
    }
    if (!lastKey.values[GAMEPAD_INPUT_UP] && key.values[GAMEPAD_INPUT_UP])
    {
      menuItem--;
      if (menuItem < 0)
        menuItem = SCN_COUNT - 1;
      ;
      refreshArrow = 1;
    }
    if ((!lastKey.values[GAMEPAD_INPUT_LEFT] && key.values[GAMEPAD_INPUT_LEFT]) || (!lastKey.values[GAMEPAD_INPUT_RIGHT] && key.values[GAMEPAD_INPUT_RIGHT]))
    {
      int v = 0;
      if (menuItem == SCN_VOLUME)
        settings_load(SettingAudioVolume, &v);
      if (menuItem == SCN_BRIGHT)
        settings_load(SettingBacklight, &v);
      if (!lastKey.values[GAMEPAD_INPUT_LEFT] && key.values[GAMEPAD_INPUT_LEFT])
        v -= 5;
      if (!lastKey.values[GAMEPAD_INPUT_RIGHT] && key.values[GAMEPAD_INPUT_RIGHT])
        v += 5;
      if (menuItem == SCN_VOLUME)
      {
        if (v < 0)
          v = 0;
        if (v > 100)
          v = 100;
        audio_volume_set(v);
        settings_save(SettingAudioVolume, v);
        vol=v;
        doRefresh = 1;
      }
      if (menuItem == SCN_BRIGHT)
      {
        if (v < 1)
          v = 1;
        if (v > 100)
          v = 100;
        settings_save(SettingBacklight, v);
        set_display_brightness(v);
        br=v;
        doRefresh = 1;
      }
    }
    if (!lastKey.values[GAMEPAD_INPUT_A] && key.values[GAMEPAD_INPUT_A])
    {
      if (menuItem == SCN_SAVE)
      {
        audio_resume();
        return MENU_SAVE_STATE;
      }

      if (menuItem == SCN_SAVE_EXIT)
        return MENU_SAVE_EXIT;

      if (menuItem == SCN_RESET)
        {
          audio_resume();
          return MENU_RESET;
        }

      if (menuItem == SCN_EXIT)
        return MENU_EXIT;

      if (menuItem == SCN_CONTINUE)
        {
          audio_resume();
          return MENU_CONTINUE;
        }

      if (menuItem == SCN_LOAD)
        {
          audio_resume();
          return MENU_LOAD;
        }
    }

    if (menuItem == SCN_VOLUME && vol != oldVol)
      top = 0;
    else if (menuItem == SCN_BRIGHT && br != oldBr)
      top = 30;
    else
      top = 60;

    if (prevItem != menuItem)
      drawBackground(top);

    //Render arrows
    int t = xTaskGetTickCount() / (200 / portTICK_PERIOD_MS);
    t = (t & 1);
    if (t != oldArrowsTick)
    {
      renderMenu(90, 29 * menuItem + 13, 65, t ? 0 : 15, 15, 15);
      oldArrowsTick = t;
    }

    if (refreshArrow)
      renderGfx((320 - 150) / 2, 0, 17, 240, menu_bg.pixel_data, 0, 0, menu_bg.width);

    if (doRefresh)
    {
      int v;
      settings_load(SettingAudioVolume, &v);
      if (top==0)
        renderMenu(141, 15, 0, 8, 65, 8);
      renderMenu(141, 15, 0, 0, (v * 65) / 100, 8);

      settings_load(SettingBacklight, &v);
      if (top==30)
        renderMenu(141, 45, 0, 8, 65, 8);
      renderMenu(141, 45, 0, 0, (v * 65) / 100, 8);
    }

    prevItem = menuItem;
    oldVol = vol;
    oldBr = br;
    doRefresh = 0;
    refreshArrow = 0;
    lastKey = key;
  }
}
