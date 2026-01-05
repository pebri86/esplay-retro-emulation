#ifndef _LCD_H_
#define _LCD_H_
#include <stdint.h>

//*****************************************************************************
//
// Make sure all of the definitions in this header have a C binding.
//
//*****************************************************************************

#ifdef __cplusplus
extern "C"
{
#endif

// The pixel number in horizontal and vertical
#define LCD_H_RES              320
#define LCD_V_RES              240

// LCD Buffer
#define LINE_BUFFERS 2
#define LINE_COUNT 6

void lcd_init();
void lcd_draw(int x1, int y1, int x2, int y2, void *data);
void wait_for_finish(void);
void backlight_deinit();
void backlight_prepare();
void set_brightness(int percent);
void backlight_prepare();
void backlight_poweroff();

#ifdef __cplusplus
}
#endif

#endif /*_DISPLAY_H_*/
