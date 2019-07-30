#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "display.h"
#include "menu_tile.h"
#include "gamepad.h"
#include "settings.h"
#include "menu.h"

void drawBackground()
{
  renderGfx((320 - 160) / 2, (240 - 80) / 2, 160, 80, menu_tile.pixel_data, 0, 0, menu_tile.width);
}

void renderMenu(int dx, int dy, int sx, int sy, int sw, int sh)
{
  renderGfx(dx, dy, sw, sh, menu_tile.pixel_data, sx, sy, menu_tile.width);
}

#define SCN_VOLUME 0
#define SCN_BRIGHT 1
#define SCN_SAVE 2
#define SCN_SAVE_EXIT 3
#define SCN_EXIT 4
#define SCN_RESET 5

#define SCN_COUNT 6
int showMenu()
{
  esp_task_wdt_feed();
  input_gamepad_state lastJoystickState;
  gamepad_read(&lastJoystickState);
  int menuItem = 0;
  int prevItem = 0;
  int scroll = 0;
  int doRefresh = 0;
  int oldArrowsTick = -1;
  int left = (320 - 80) / 2;
  int top = (240 - 40) / 2;
  int initMenu = 0;

  while (1)
  {
    esp_task_wdt_feed();
    input_gamepad_state joystick;
    gamepad_read(&joystick);

    //hack for display first item
    if (!initMenu)
    {
      drawBackground();
      renderMenu(left, top, 0, (40 * menuItem) + 80, 80, 40);
      int v=0;
			if (menuItem==SCN_VOLUME) v=get_volume_settings();
			if (v<0) v=0;
			if (v>4) v=4;
      renderMenu(((320-62)/2)+1, ((240-7)/2)+9, 0, 345, 62, 7);
			renderMenu(((320-62)/2)+1, ((240-7)/2)+9, 0, 338, (v*62)/4, 7);
      initMenu = 1;
    }
    if (doRefresh)
      drawBackground();
    if (!lastJoystickState.values[GAMEPAD_INPUT_UP] && joystick.values[GAMEPAD_INPUT_UP])
    {
      menuItem++;
      if (menuItem >= SCN_COUNT)
        menuItem = 0;
      doRefresh = 1;
    }
    if (!lastJoystickState.values[GAMEPAD_INPUT_DOWN] && joystick.values[GAMEPAD_INPUT_DOWN])
    {
      menuItem--;
      if (menuItem < 0)
        menuItem = SCN_COUNT - 1;
      ;
      doRefresh = 1;
    }
    if ((!lastJoystickState.values[GAMEPAD_INPUT_LEFT] && joystick.values[GAMEPAD_INPUT_LEFT]) || (!lastJoystickState.values[GAMEPAD_INPUT_RIGHT] && joystick.values[GAMEPAD_INPUT_RIGHT]))
    {
      int v = 0;
      if (menuItem == SCN_VOLUME)
        v = get_volume_settings();
      if (menuItem == SCN_BRIGHT)
        v = get_backlight_settings();
      if (!lastJoystickState.values[GAMEPAD_INPUT_LEFT] && joystick.values[GAMEPAD_INPUT_LEFT])
        v -= 1;
      if (!lastJoystickState.values[GAMEPAD_INPUT_RIGHT] && joystick.values[GAMEPAD_INPUT_RIGHT])
        v += 1;
      if (menuItem == SCN_VOLUME)
      {
        if (v < 0)
          v = 0;
        if (v > 4)
          v = 4;
        set_volume_settings(v);
        doRefresh = 1;
      }
      if (menuItem == SCN_BRIGHT)
      {
        if (v < 1)
          v = 1;
        if (v > 100)
          v = 100;
        set_backlight_settings(v);
        set_display_brightness(v);
        doRefresh = 1;
      }
    }
    if (!lastJoystickState.values[GAMEPAD_INPUT_A] && joystick.values[GAMEPAD_INPUT_A])
    {
      if (menuItem == SCN_SAVE)
      {
        return MENU_SAVE_STATE;
      }
      if (menuItem == SCN_SAVE_EXIT)
      {
        return MENU_SAVE_EXIT;
      }
      if (menuItem == SCN_RESET)
      {
        return MENU_RESET;
      }
      if (menuItem == SCN_EXIT)
      {
        return MENU_EXIT;
      }
    }
    if (!lastJoystickState.values[GAMEPAD_INPUT_B] && joystick.values[GAMEPAD_INPUT_B])
    {
      return MENU_CONTINUE;
    }
    if (prevItem != menuItem)
      renderMenu(left, top, 0, (40 * menuItem) + 80, 80, 40);

    //Render arrows
    int t = xTaskGetTickCount() / (400 / portTICK_PERIOD_MS);
    t = (t & 1);
    if (t != oldArrowsTick)
    {
      renderMenu((320 - 18) / 2, ((240 - 80) / 2) - 18, t ? 0 : 18, 4 * 80, 18, 18);
      renderMenu((320 - 18) / 2, ((240 - 80) / 2) + 80, t ? 36 : 54, 4 * 80, 18, 18);
      oldArrowsTick = t;
    }

    if (doRefresh && (menuItem==SCN_VOLUME)) {
			int v=0;
			if (menuItem==SCN_VOLUME) v=get_volume_settings();
			if (v<0) v=0;
			if (v>4) v=4;
      renderMenu(((320-62)/2)+1, ((240-7)/2)+9, 0, 345, 62, 7);
			renderMenu(((320-62)/2)+1, ((240-7)/2)+9, 0, 338, (v*62)/4, 7);
		}
		if (doRefresh && (menuItem==SCN_BRIGHT)) {
			int v=0;
			if (menuItem==SCN_BRIGHT) v=get_backlight_settings();
			if (v<1) v=1;
			if (v>100) v=100;
      renderMenu(((320-62)/2)+1, ((240-7)/2)+9, 0, 345, 62, 7);
			renderMenu(((320-62)/2)+1, ((240-7)/2)+9, 0 , 338, (v*62)/100, 7);
		}

    prevItem = menuItem;
    doRefresh = 0;
    lastJoystickState = joystick;
  }
}
