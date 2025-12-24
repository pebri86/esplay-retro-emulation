#include "power.h"
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "gamepad.h"

// New ADC headers for 5.x
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static bool input_battery_initialized = false;
static float adc_value = 0.0f;
static float forced_adc_value = 0.0f;
static bool battery_monitor_enabled = true;
static bool system_initialized = false;

// ADC Handles for 5.5.1
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool do_calibration = false;

void system_sleep()
{
    printf("%s: Entered.\n", __func__);

    input_gamepad_state joystick;
    gamepad_read(&joystick);

    while (joystick.values[GAMEPAD_INPUT_MENU])
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        gamepad_read(&joystick);
    }

    printf("%s: Configuring deep sleep.\n", __func__);

    // Note: MENU must be a valid RTC-capable GPIO
    esp_err_t err = esp_sleep_enable_ext0_wakeup(MENU, 0);

    if (err != ESP_OK)
    {
        printf("%s: esp_sleep_enable_ext0_wakeup failed.\n", __func__);
        abort();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
}

void esplay_system_init()
{
    if (rtc_gpio_is_valid_gpio(MENU))
    {
        rtc_gpio_deinit(MENU);
    }
    system_initialized = true;
}

void system_led_set(int state)
{
    gpio_set_level(LED1, state);
}

charging_state getChargeStatus()
{
    if (!gpio_get_level(USB_PLUG_PIN))
        return NO_CHRG;
    else
    {
        if (!gpio_get_level(CHRG_STATE_PIN))
            return CHARGING;
        else
            return FULL_CHARGED;
    }
}

static void battery_monitor_task(void *pvParameters)
{
    bool led_state = false;
    charging_state chrg;
    int fullCtr = 0;
    int fixFull = 0;

    while (true)
    {
        if (battery_monitor_enabled)
        {
            battery_state battery;
            battery_level_read(&battery);

            if (battery.percentage < 2)
            {
                led_state = !led_state;
                system_led_set(led_state);
            }
            else if (led_state)
            {
                led_state = false;
                system_led_set(led_state);
            }
            else
            {
                chrg = getChargeStatus();
                if (chrg == FULL_CHARGED || fixFull)
                {
                    fullCtr++;
                    system_led_set(0);
                }

                if (chrg == CHARGING)
                {
                    fullCtr = 0;
                    system_led_set(1);
                }

                if (fullCtr == 32)
                {
                    fixFull = 1;
                }

                if (chrg == NO_CHRG)
                {
                    system_led_set(0);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void battery_level_init()
{
    // GPIO Config
    gpio_reset_pin(LED1);
    gpio_set_direction(LED1, GPIO_MODE_OUTPUT);

    gpio_reset_pin(USB_PLUG_PIN);
    gpio_set_direction(USB_PLUG_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(USB_PLUG_PIN, GPIO_PULLUP_ONLY);

    gpio_reset_pin(CHRG_STATE_PIN);
    gpio_set_direction(CHRG_STATE_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(CHRG_STATE_PIN, GPIO_PULLUP_ONLY);

    // ADC Oneshot Unit Init
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    // ADC Channel Config
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, // Replaces ADC_ATTEN_11db
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_PIN, &config));

    // Calibration Init (Curve Fitting is preferred for ESP32-S3/C3/v3)
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t cal_ret = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
    if (cal_ret == ESP_OK)
    {
        do_calibration = true;
    }

    input_battery_initialized = true;
    battery_monitor_enabled_set(true);
    xTaskCreatePinnedToCore(&battery_monitor_task, "battery_monitor", 2048, NULL, 5, NULL, 1);
}

void battery_level_read(battery_state *out_state)
{
    if (!input_battery_initialized)
    {
        printf("battery_level_read: not initialized.\n");
        abort();
    }

    const int sampleCount = 8;
    int voltage_mv = 0;
    int total_voltage_mv = 0;

    for (int i = 0; i < sampleCount; ++i)
    {
        int raw;
        adc_oneshot_read(adc1_handle, ADC_PIN, &raw);
        if (do_calibration)
        {
            adc_cali_raw_to_voltage(adc1_cali_handle, raw, &voltage_mv);
        }
        else
        {
            // Fallback if calibration fails: raw * Vref / 4095
            voltage_mv = (raw * 3300) / 4095;
        }
        total_voltage_mv += voltage_mv;
    }

    float adcSample = (total_voltage_mv / (float)sampleCount) / 1000.0f;

    if (adc_value == 0.0f)
    {
        adc_value = adcSample;
    }
    else
    {
        adc_value = (adc_value + adcSample) / 2.0f;
    }

    const float R1 = 100000;
    const float R2 = 100000;
    const float Vo = adc_value;
    const float Vs = (forced_adc_value > 0.0f) ? (forced_adc_value) : (Vo * (R1 + R2) / R2);

    const float FullVoltage = 4.1f;
    const float EmptyVoltage = 3.4f;

    out_state->millivolts = (int)(Vs * 1000);
    out_state->percentage = (int)((Vs - EmptyVoltage) / (FullVoltage - EmptyVoltage) * 100.0f);

    if (out_state->percentage > 100)
        out_state->percentage = 100;
    if (out_state->percentage < 0)
        out_state->percentage = 0;

    out_state->state = getChargeStatus();
}

void battery_level_force_voltage(float volts)
{
    forced_adc_value = volts;
}

void battery_monitor_enabled_set(int value)
{
    battery_monitor_enabled = (bool)value;
}