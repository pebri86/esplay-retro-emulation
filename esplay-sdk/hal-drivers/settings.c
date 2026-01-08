/**
 * @file settings.c
 * @brief Settings and System Utilities for ESP-IDF 5.5.1
 */

/*********************
 * INCLUDES
 *********************/
#include "settings.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

/*********************
 * DEFINES
 *********************/
static const char *TAG = "settings";
#define NVS_NAMESPACE "esplay"

/**********************
 * TYPEDEFS
 **********************/
typedef enum {
  TypeInt,
  TypeStr,
} KeyType;

/**********************
 * STATIC VARIABLES
 **********************/
static nvs_handle_t handle; // Handle type changed to nvs_handle_t in modern IDF

static KeyType settings_types[SettingMax] = {TypeInt, TypeInt, TypeInt, TypeStr,
                                             TypeInt, TypeInt, TypeInt};

static const char *settings_keys[SettingMax] = {
    "volume", "backlight", "playmode", "rom_name",
    "scale",  "wifi",      "scale_alg"};

/**********************
 * GLOBAL FUNCTIONS
 **********************/

/**
 * @brief Initialize NVS settings
 */
int settings_init(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS Init failed: %s", esp_err_to_name(err));
    return -1;
  }

  err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS Open failed: %s", esp_err_to_name(err));
    return -1;
  }

  return 0;
}

void settings_deinit(void) {
  nvs_close(handle);
  nvs_flash_deinit();
}

int settings_load(Setting setting, int32_t *value_out) {
  assert(setting < SettingMax && setting >= 0);
  assert(settings_types[setting] == TypeInt);

  esp_err_t err = nvs_get_i32(handle, settings_keys[setting], value_out);
  if (err == ESP_ERR_NVS_NOT_FOUND)
    return -1;
  return (err == ESP_OK) ? 0 : -1;
}

int settings_save(Setting setting, int32_t value) {
  assert(setting < SettingMax && setting >= 0);
  assert(settings_types[setting] == TypeInt);

  esp_err_t err = nvs_set_i32(handle, settings_keys[setting], value);
  if (err != ESP_OK)
    return -1;

  return (nvs_commit(handle) == ESP_OK) ? 0 : -1;
}

char *settings_load_str(Setting setting) {
  size_t len;
  assert(setting < SettingMax && setting >= 0);
  assert(settings_types[setting] == TypeStr);

  // Get length
  if (nvs_get_str(handle, settings_keys[setting], NULL, &len) != ESP_OK) {
    return NULL;
  }

  char *value_out = malloc(len);
  if (!value_out)
    return NULL;

  if (nvs_get_str(handle, settings_keys[setting], value_out, &len) != ESP_OK) {
    free(value_out);
    return NULL;
  }

  return value_out;
}

int settings_save_str(Setting setting, const char *value) {
  assert(setting < SettingMax && setting >= 0);
  assert(settings_types[setting] == TypeStr);

  esp_err_t err = nvs_set_str(handle, settings_keys[setting], value);
  if (err != ESP_OK)
    return -1;

  return (nvs_commit(handle) == ESP_OK) ? 0 : -1;
}

void system_application_set(int slot) {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_MIN + slot, NULL);

  if (partition != NULL) {
    esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "OTA Boot Partition set failed: %s", esp_err_to_name(err));
      abort();
    }
  }
}

/**********************
 * UTIL FUNCTIONS
 **********************/

/**
 * @brief Get filename from path using standard string functions
 */
char *system_util_GetFileName(const char *path) {
  const char *last_slash = strrchr(path, '/');
  const char *start = (last_slash) ? last_slash + 1 : path;
  return strdup(start);
}

/**
 * @brief Get file extension (including '.')
 */
char *system_util_GetFileExtenstion(const char *path) {
  const char *last_dot = strrchr(path, '.');
  if (!last_dot)
    return strdup("");
  return strdup(last_dot);
}

/**
 * @brief Get filename without extension
 */
char *system_util_GetFileNameWithoutExtension(const char *path) {
  char *full_name = system_util_GetFileName(path);
  char *last_dot = strrchr(full_name, '.');

  if (last_dot) {
    *last_dot = '\0'; // Terminate string at the dot
  }

  // We re-strdup to shrink the allocation to the new size
  char *result = strdup(full_name);
  free(full_name);
  return result;
}
