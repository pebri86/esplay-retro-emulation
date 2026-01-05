idf_component_register(SRCS "launcher_main.c" "apps.c" "games.c" "settings.c" "fnmatch.c"
                    INCLUDE_DIRS ""
                    REQUIRES esp_timer hal-drivers appfs lvgl nvs_flash esp_wifi file_server app_update)
