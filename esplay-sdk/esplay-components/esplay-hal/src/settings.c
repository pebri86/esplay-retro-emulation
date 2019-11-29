/**
 * @file settings.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <settings.h>


#include <nvs.h>
#include "nvs_flash.h"
#include <assert.h>
#include <esp_heap_caps.h>

#include <esp_partition.h>
#include <esp_ota_ops.h>

#include <string.h>
/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef enum KeyType {
	TypeInt,
	TypeStr,
} KeyType;

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
static nvs_handle handle;

static KeyType settings_types[SettingMax] = {
    TypeInt, TypeInt, TypeInt, TypeStr, TypeInt, TypeInt,
};

static char *settings_keys[SettingMax] = {
    "volume", "backlight", "playmode", "rom_name", "scale", "wifi",
};

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
// Initalize settings
int settings_init(void)
{
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		// NVS partition was truncated and needs to be erased
		// Retry nvs_flash_init
		if (nvs_flash_erase() != ESP_OK) {
			return -1;
		}
		err = nvs_flash_init();
	}
	if (err != ESP_OK) {
		return -1;
	}

	if (nvs_open("ogo-shell", NVS_READWRITE, &handle) != ESP_OK) {
		return -1;
	}

	return 0;
}

/// Initalize settings
void settings_deinit(void)
{
	nvs_close(handle);
	nvs_flash_deinit();
}

/// Load setting.
/// Return 0 if loaded, false if not found
int settings_load(Setting setting, int32_t *value_out)
{
	assert(setting < SettingMax && setting >= 0);
	assert(settings_types[setting] == TypeInt);
	return nvs_get_i32(handle, settings_keys[setting], value_out);
}

/// Save setting.
/// Return 0 if saving was sucessfull
int settings_save(Setting setting, int32_t value)
{
	assert(setting < SettingMax && setting >= 0);
	assert(settings_types[setting] == TypeInt);
	return nvs_set_i32(handle, settings_keys[setting], value);
}

/// Load string setting.
/// Return 0 if saving was sucessfull
int settings_load_str(Setting setting, char *value_out, size_t value_len)
{
	size_t len;
	assert(setting < SettingMax && setting >= 0);
	assert(settings_types[setting] == TypeStr);
	nvs_get_str(handle, settings_keys[setting], NULL, &len);
	if (len > value_len) {
		return -1;
	}
	return nvs_get_str(handle, settings_keys[setting], value_out, &len);
}

/// Save string setting.
/// Return 0 if saving was sucessfull
int settings_save_str(Setting setting, const char *value)
{
	assert(setting < SettingMax && setting >= 0);
	assert(settings_types[setting] == TypeStr);
	return nvs_set_str(handle, settings_keys[setting], value);
}

/// Boot application from selected partition slot
void system_application_set(int slot)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_MIN + slot,
        NULL);
    if (partition != NULL)
    {
        esp_err_t err = esp_ota_set_boot_partition(partition);
        if (err != ESP_OK)
        {
            printf("%s: esp_ota_set_boot_partition failed.\n", __func__);
            abort();
        }
    }
}

/// Utils function get filename from given path
char *system_util_GetFileName(const char *path)
{
    int length = strlen(path);
    int fileNameStart = length;

    if (fileNameStart < 1)
        abort();

    while (fileNameStart > 0)
    {
        if (path[fileNameStart] == '/')
        {
            ++fileNameStart;
            break;
        }

        --fileNameStart;
    }

    int size = length - fileNameStart + 1;

    char *result = malloc(size);
    if (!result)
        abort();

    result[size - 1] = 0;
    for (int i = 0; i < size - 1; ++i)
    {
        result[i] = path[fileNameStart + i];
    }

    //printf("GetFileName: result='%s'\n", result);

    return result;
}

/// Utils function get file extension from given path
char *system_util_GetFileExtenstion(const char *path)
{
    // Note: includes '.'
    int length = strlen(path);
    int extensionStart = length;

    if (extensionStart < 1)
        abort();

    while (extensionStart > 0)
    {
        if (path[extensionStart] == '.')
        {
            break;
        }

        --extensionStart;
    }

    int size = length - extensionStart + 1;

    char *result = malloc(size);
    if (!result)
        abort();

    result[size - 1] = 0;
    for (int i = 0; i < size - 1; ++i)
    {
        result[i] = path[extensionStart + i];
    }

    //printf("GetFileExtenstion: result='%s'\n", result);

    return result;
}

/// Utils function get filename from given path without extension
char *system_util_GetFileNameWithoutExtension(const char *path)
{
    char *fileName = system_util_GetFileName(path);

    int length = strlen(fileName);
    int extensionStart = length;

    if (extensionStart < 1)
        abort();

    while (extensionStart > 0)
    {
        if (fileName[extensionStart] == '.')
        {
            break;
        }

        --extensionStart;
    }

    int size = extensionStart + 1;

    char *result = malloc(size);
    if (!result)
        abort();

    result[size - 1] = 0;
    for (int i = 0; i < size - 1; ++i)
    {
        result[i] = fileName[i];
    }

    free(fileName);

    //printf("GetFileNameWithoutExtension: result='%s'\n", result);

    return result;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
