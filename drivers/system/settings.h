/**
 * @file settings.h
 *
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/*********************
 *      INCLUDES
 *********************/
#include <stdint.h>

    /*********************
 *      DEFINES
 *********************/

    /**********************
 *      TYPEDEFS
 **********************/
    typedef enum
    {
        ESPLAY_VOLUME_LEVEL0 = 0,
        ESPLAY_VOLUME_LEVEL1 = 1,
        ESPLAY_VOLUME_LEVEL2 = 2,
        ESPLAY_VOLUME_LEVEL3 = 3,
        ESPLAY_VOLUME_LEVEL4 = 4,

        _ODROID_VOLUME_FILLER = 0xffffffff
    } esplay_volume_level;

    /**********************
 * GLOBAL PROTOTYPES
 **********************/
    void system_application_set(int slot);
    int32_t get_backlight_settings();
    void set_backlight_settings(int32_t value);
    char *system_util_GetFileName(const char *path);
    char *system_util_GetFileExtenstion(const char *path);
    char *system_util_GetFileNameWithoutExtension(const char *path);
    int8_t get_menu_flag_settings();
    void set_menu_flag_settings(int8_t value);
    int8_t get_volume_settings();
    void set_volume_settings(int8_t value);
    char *get_rom_name_settings();
    void set_rom_name_settings(char *value);

    /**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*SETTINGS_H*/
