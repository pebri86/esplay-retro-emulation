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
        SCALE_NONE = 0,
        SCALE_FIT = 1,
        SCALE_STRETCH = 2
    } esplay_scale_option;

    /**********************
 * GLOBAL PROTOTYPES
 **********************/
    void system_application_set(int slot);
    int32_t get_backlight_settings();
    void set_backlight_settings(int32_t value);
    char *system_util_GetFileName(const char *path);
    char *system_util_GetFileExtenstion(const char *path);
    char *system_util_GetFileNameWithoutExtension(const char *path);
    int get_volume_settings();
    void set_volume_settings(int value);
    char *get_rom_name_settings();
    void set_rom_name_settings(char *value);
    esplay_scale_option get_scale_option_settings();
    void set_scale_option_settings(esplay_scale_option scale);
    void set_wifi_settings(uint8_t value);
    uint8_t get_wifi_settings();
    void set_last_emu_settings(uint8_t value);
    uint8_t get_last_emu_settings();
    void set_last_rom_settings(uint8_t value);
    uint8_t get_last_rom_settings();

    /**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*SETTINGS_H*/
