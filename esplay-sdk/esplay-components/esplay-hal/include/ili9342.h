/**
 * @file lv_templ.h
 *
 */

#ifndef __ILI9342_H__
#define __ILI9342_H__
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
#define ILI9342_HOR_RES 320
#define ILI9342_VER_RES 240

    /**********************
 *      TYPEDEFS
 **********************/

    /**********************
 * GLOBAL PROTOTYPES
 **********************/
    int ili9342_is_backlight_initialized();
    void ili9342_backlight_percentage_set(int value);
    void ili9342_init(void);
    void ili9342_backlight_deinit();
    void ili9342_prepare();
    void ili9342_poweroff();

    /**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ILI9342_H*/
