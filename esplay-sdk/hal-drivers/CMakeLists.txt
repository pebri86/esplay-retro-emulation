set(include_dirs "include")

set(srcs
    "lcd.c"
    "gamepad.c"
    "sdcard.c"
    "power.c"
    "settings.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${include_dirs}"
                    REQUIRES esp_lcd esp_event fatfs nvs_flash app_update
                    PRIV_REQUIRES esp_driver_ledc esp_driver_gpio esp_driver_i2c esp_adc)
