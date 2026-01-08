#include "sdcard.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool isOpen = false;
static sdmmc_card_t *card = NULL;
static char *mounted_path = NULL;
static const char *TAG = "hal-sdcard";

// Sorting Helpers
static int strcicmp(char const *a, char const *b) {
  for (;; a++, b++) {
    int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
    if (d != 0 || !*a)
      return d;
  }
}

static void quick_sort(char *arr[], int low, int high) {
  if (low < high) {
    char *pivot = arr[high];
    int i = low - 1;
    for (int j = low; j < high; j++) {
      if (strcicmp(arr[j], pivot) < 0) {
        i++;
        char *tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
      }
    }
    char *tmp = arr[i + 1];
    arr[i + 1] = arr[high];
    arr[high] = tmp;
    int pi = i + 1;
    quick_sort(arr, low, pi - 1);
    quick_sort(arr, pi + 1, high);
  }
}

// SD Card Logic
esp_err_t sdcard_open(const char *base_path) {
  if (isOpen)
    return ESP_OK;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = CONFIG_WL_SECTOR_SIZE};

  esp_err_t ret = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot_config,
                                          &mount_config, &card);
  if (ret == ESP_OK) {
    isOpen = true;
    mounted_path = strdup(base_path);
    ESP_LOGI(TAG, "SDCard mounted at %s", base_path);
  }
  return ret;
}

esp_err_t sdcard_close() {
  if (!isOpen)
    return ESP_FAIL;
  esp_err_t ret = esp_vfs_fat_sdcard_unmount(mounted_path, card);
  if (ret == ESP_OK) {
    isOpen = false;
    free(mounted_path);
    mounted_path = NULL;
  }
  return ret;
}

void sdcard_get_free_space(uint32_t *tot, uint32_t *free_spc) {
  FATFS *fs;
  DWORD fre_clust;
  if (f_getfree("", &fre_clust, &fs) == FR_OK) {
    *tot = ((fs->n_fatent - 2) * fs->csize) / 2;
    *free_spc = (fre_clust * fs->csize) / 2;
  }
}

int sdcard_files_get(const char *path, const char *extension,
                     char ***filesOut) {
  const int MAX_FILES = 1024;
  int count = 0;
  char **result = malloc(MAX_FILES * sizeof(char *));
  DIR *dir = opendir(path);
  if (!dir || !result)
    return 0;

  struct dirent *entry;
  int ext_len = strlen(extension);

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    int name_len = strlen(entry->d_name);
    if (name_len <= ext_len)
      continue;

    char *file_ext = &entry->d_name[name_len - ext_len];
    if (strcasecmp(file_ext, extension) == 0) {
      result[count] = strdup(entry->d_name);
      if (++count >= MAX_FILES)
        break;
    }
  }
  closedir(dir);
  if (count > 0)
    quick_sort(result, 0, count - 1);
  *filesOut = result;
  return count;
}

void sdcard_files_free(char **files, int count) {
  for (int i = 0; i < count; i++)
    free(files[i]);
  free(files);
}

size_t sdcard_get_filesize(const char *path) {
  struct stat st;
  return (stat(path, &st) == 0) ? st.st_size : 0;
}

size_t sdcard_copy_file_to_memory(const char *path, void *ptr) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;

  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  fseek(f, 0, SEEK_SET);

  size_t read = fread(ptr, 1, size, f);
  fclose(f);
  return read;
}

char *sdcard_create_savefile_path(const char *base_path, const char *fileName) {
  char *dot = strrchr(fileName, '.');
  if (!dot)
    return NULL;

  char ext[16];
  strncpy(ext, dot + 1, sizeof(ext));

  char *path;
  asprintf(&path, "%s/esplay/data/%s/%s.sav", base_path, ext, fileName);
  return path;
}
