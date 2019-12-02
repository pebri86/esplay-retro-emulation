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
#include <stddef.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef enum esplay_scale_option {
   SCALE_NONE = 0,
   SCALE_FIT = 1,
   SCALE_STRETCH = 2
} esplay_scale_option;

typedef enum Setting {
	SettingAudioVolume = 0,
	SettingBacklight,
	SettingPlayingMode,
	SettingRomPath,
	SettingScaleMode,
	SettingWifi,
	SettingMax,
} Setting;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/// Initalize settings
int settings_init(void);

/// Initalize settings
void settings_deinit(void);

/// Load setting.
/// Return 0 if loaded, false if not found
int settings_load(Setting setting, int32_t *value_out);

/// Save setting.
/// Return 0 if saving was sucessfull
int settings_save(Setting setting, int32_t value);

/// Load string setting.
/// Return 0 if saving was sucessfull
char *settings_load_str(Setting setting);

/// Save string setting.
/// Return 0 if saving was sucessfull
int settings_save_str(Setting setting, const char *value);

/// Boot application from selected partition slot
void system_application_set(int slot);

/// Utils function get filename from given path
char *system_util_GetFileName(const char *path);

/// Utils function get file extension from given path
char *system_util_GetFileExtenstion(const char *path);

/// Utils function get filename from given path without extension
char *system_util_GetFileNameWithoutExtension(const char *path);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*SETTINGS_H*/
