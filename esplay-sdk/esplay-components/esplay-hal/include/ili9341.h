/**
 * @file lv_templ.h
 *
 */

#ifndef __ILI9341_H__
#define __ILI9341_H__
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*********************
 *      INCLUDES
 *********************/

/*********************
 *      DEFINES
 *********************/
#define ILI9341_HOR_RES 320
#define ILI9341_VER_RES 240

    /**********************
 *      TYPEDEFS
 **********************/

    /**********************
 * GLOBAL PROTOTYPES
 **********************/
    int ili9341_is_backlight_initialized();
    void ili9341_backlight_percentage_set(int value);
    void ili9341_init(void);
    void ili9341_backlight_deinit();
    void ili9341_prepare();
    void ili9341_poweroff();

    /**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ILI9341_H*/
