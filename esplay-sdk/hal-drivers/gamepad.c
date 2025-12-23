#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"   // New v5.x I2C Driver
#include "esp_adc/adc_oneshot.h"  // New v5.x ADC Driver
#include "esp_log.h"
#include "sdkconfig.h"
#include "gamepad.h"

static const char *TAG = "hal-gamepad";

// Configuration Constants
static gpio_num_t i2c_gpio_sda = GPIO_NUM_21;
static gpio_num_t i2c_gpio_scl = GPIO_NUM_22;
static uint32_t i2c_frequency = 100000;
static i2c_port_t i2c_port_num = I2C_NUM_0;

// Global Handles for v5.x
static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;
#ifdef CONFIG_ESPLAY20_HW
static adc_oneshot_unit_handle_t adc1_handle;
#endif

static volatile bool input_task_is_running = false;
static volatile input_gamepad_state gamepad_state;
static uint8_t debounce[GAMEPAD_INPUT_MAX];
static volatile bool input_gamepad_initialized = false;
static SemaphoreHandle_t xSemaphore;

/**
 * @brief Initialize I2C Master using the New Driver
 */
static void i2c_master_driver_initialize()
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = i2c_port_num,
        .scl_io_num = i2c_gpio_scl,
        .sda_io_num = i2c_gpio_sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x20, // TCA9555 or similar expander
        .scl_speed_hz = i2c_frequency,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));
}

/**
 * @brief Read keypad state via I2C
 */
static uint8_t i2c_keypad_read()
{
    uint8_t data = 0xFF;
    // New unified API replaces cmd_link_create/start/stop
    esp_err_t err = i2c_master_receive(dev_handle, &data, 1, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(err));
    }
    return data;
}

/**
 * @brief Read hardware states (ADC and GPIO)
 */
input_gamepad_state gamepad_input_read_raw()
{
    input_gamepad_state state = {0};

    #ifdef CONFIG_ESPLAY20_HW
    int joyX = 0, joyY = 0;
    // ADC1 Oneshot read
    adc_oneshot_read(adc1_handle, IO_X, &joyX);
    adc_oneshot_read(adc1_handle, IO_Y, &joyY);

    // Joystick Logic
    if (joyX > 3072) {
        state.values[GAMEPAD_INPUT_LEFT] = 1;
    } else if (joyX > 1024) {
        state.values[GAMEPAD_INPUT_RIGHT] = 1;
    }

    if (joyY > 3072) {
        state.values[GAMEPAD_INPUT_UP] = 1;
    } else if (joyY > 1024) {
        state.values[GAMEPAD_INPUT_DOWN] = 1;
    }

    state.values[GAMEPAD_INPUT_SELECT] = !(gpio_get_level(SELECT));
    state.values[GAMEPAD_INPUT_START] = !(gpio_get_level(START));
    state.values[GAMEPAD_INPUT_A] = !(gpio_get_level(A));
    state.values[GAMEPAD_INPUT_B] = !(gpio_get_level(B));
    #endif

    // Read I2C Expanders
    uint8_t i2c_data = i2c_keypad_read();
    for (int i = 0; i < 8; ++i) {
        state.values[i] = ((i2c_data & (1 << i)) == 0) ? 1 : 0;
    }

    // Direct GPIOs
    state.values[GAMEPAD_INPUT_MENU] = !(gpio_get_level(MENU));
    state.values[GAMEPAD_INPUT_L] = !(gpio_get_level(L_BTN));
    state.values[GAMEPAD_INPUT_R] = !(gpio_get_level(R_BTN));

    return state;
}

/**
 * @brief Background task for polling and debouncing
 */
static void input_task(void *arg)
{
    input_task_is_running = true;
    memset(debounce, 0xFF, sizeof(debounce));

    while (input_task_is_running)
    {
        input_gamepad_state raw_state = gamepad_input_read_raw();

        if (xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            for (int i = 0; i < GAMEPAD_INPUT_MAX; ++i)
            {
                debounce[i] = (debounce[i] << 1) | (raw_state.values[i] ? 1 : 0);
                uint8_t val = debounce[i] & 0x03; 
                
                if (val == 0x00) {
                    gamepad_state.values[i] = 0;
                } else if (val == 0x03) {
                    gamepad_state.values[i] = 1;
                }
            }
            xSemaphoreGive(xSemaphore);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    input_gamepad_initialized = false;
    vTaskDelete(NULL);
}

void gamepad_read(input_gamepad_state *out_state)
{
    if (!input_gamepad_initialized) return;

    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        *out_state = gamepad_state;
        xSemaphoreGive(xSemaphore);
    }
}

void gamepad_init()
{
    xSemaphore = xSemaphoreCreateMutex();
    if (xSemaphore == NULL) abort();

    // 1. Initialize ADC
    #ifdef CONFIG_ESPLAY20_HW
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_config_t adc_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // v5: DB_11 is now DB_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, IO_X, &adc_config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, IO_Y, &adc_config));
    #endif

    // 2. Initialize I2C
    i2c_master_driver_initialize();

    // 3. Initialize GPIOs
    gpio_config_t btn_config = {
        .intr_type = GPIO_INTR_DISABLE, // Polling used, so disable interrupts
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask = ((1ULL << L_BTN) | (1ULL << R_BTN) | (1ULL << MENU))
    };
    
    #ifdef CONFIG_ESPLAY20_HW
    btn_config.pin_bit_mask |= ((1ULL << A) | (1ULL << B) | (1ULL << SELECT) | (1ULL << START));
    #endif
    
    gpio_config(&btn_config);

    input_gamepad_initialized = true;

    // 4. Start Task
    xTaskCreatePinnedToCore(&input_task, "input_task", 1024 * 3, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Gamepad initialized successfully on ESP-IDF v5.5.1");
}

void input_gamepad_terminate()
{
    input_task_is_running = false;
    // Small delay to allow task to exit
    vTaskDelay(pdMS_TO_TICKS(50));

    if (dev_handle) {
        i2c_master_bus_rm_device(dev_handle);
    }
    if (bus_handle) {
        i2c_del_master_bus(bus_handle);
    }
#ifdef CONFIG_ESPLAY20_HW
    if (adc1_handle) {
        adc_oneshot_del_unit(adc1_handle);
    }
#endif
    if (xSemaphore) {
        vSemaphoreDelete(xSemaphore);
    }
}