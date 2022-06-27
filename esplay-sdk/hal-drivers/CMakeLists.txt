set(include_dirs "include")

set(srcs 
    "lcd.c"
    "gamepad.c"
    "sdcard.c"
    "power.c"
    "settings.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${include_dirs}"
                    REQUIRES esp_adc_cal fatfs nvs_flash app_update)