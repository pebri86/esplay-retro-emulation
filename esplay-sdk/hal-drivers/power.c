#include "power.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "gamepad.h"

// New ADC headers for 5.x
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "power";

static bool input_battery_initialized = false;
static float adc_value = 0.0f;
static float forced_adc_value = 0.0f;
static bool battery_monitor_enabled = true;

// ADC Handles for 5.5.1
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool do_calibration = false;

/**
 * @brief Enter Deep Sleep. Wakes up on MENU button press.
 */
void system_sleep() {
  ESP_LOGI(TAG, "Preparing for deep sleep...");

  input_gamepad_state joystick;
  gamepad_read(&joystick);

  // Wait for MENU button release to avoid immediate wakeup
  while (joystick.values[GAMEPAD_INPUT_MENU]) {
    vTaskDelay(pdMS_TO_TICKS(10));
    gamepad_read(&joystick);
  }

  // Configure wakeup: MENU button pulls to GND (0)
  // Ensure MENU is defined as an RTC_GPIO in your config
  esp_err_t err = esp_sleep_enable_ext0_wakeup(MENU, 0);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Sleep config failed: %s", esp_err_to_name(err));
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(100));
  esp_deep_sleep_start();
}

void esplay_system_init() {
  if (rtc_gpio_is_valid_gpio(MENU)) {
    rtc_gpio_deinit(MENU);
  }
}

void system_led_set(int state) { gpio_set_level(LED1, state); }

charging_state getChargeStatus() {
  // Active low logic usually applies to hardware status pins
  if (!gpio_get_level(USB_PLUG_PIN))
    return NO_CHRG;

  if (!gpio_get_level(CHRG_STATE_PIN))
    return CHARGING;

  return FULL_CHARGED;
}

static void battery_monitor_task(void *pvParameters) {
  bool led_state = false;
  int fullCtr = 0;
  bool fixFull = false;

  while (true) {
    if (battery_monitor_enabled) {
      battery_state battery;
      battery_level_read(&battery);

      // Low battery warning: Blink LED
      if (battery.percentage < 2) {
        led_state = !led_state;
        system_led_set(led_state);
      } else {
        charging_state chrg = getChargeStatus();

        if (chrg == FULL_CHARGED || fixFull) {
          fullCtr++;
          system_led_set(0);
          if (fullCtr >= 32)
            fixFull = true;
        } else if (chrg == CHARGING) {
          fullCtr = 0;
          fixFull = false;
          system_led_set(1); // Solid LED while charging
        } else               // NO_CHRG
        {
          system_led_set(0);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void battery_level_init() {
  // 1. GPIO Configuration
  gpio_config_t power_io_cfg = {
      .pin_bit_mask = (1ULL << LED1),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = 0,
      .pull_down_en = 0,
  };
  gpio_config(&power_io_cfg);

  gpio_config_t input_io_cfg = {
      .pin_bit_mask = (1ULL << USB_PLUG_PIN) | (1ULL << CHRG_STATE_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = 1,
      .pull_down_en = 0,
  };
  gpio_config(&input_io_cfg);

  // 2. ADC Oneshot Unit Init
  adc_oneshot_unit_init_cfg_t init_config1 = {
      .unit_id = ADC_UNIT_1,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

  // 3. ADC Channel Config
  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_12,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_PIN, &config));

  // 4. Calibration Init (Line Fitting is standard for ESP32/ESP32-S3)
  adc_cali_line_fitting_config_t cali_config = {
      .unit_id = ADC_UNIT_1,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };

  if (adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle) ==
      ESP_OK) {
    do_calibration = true;
  }

  input_battery_initialized = true;
  xTaskCreatePinnedToCore(&battery_monitor_task, "bat_task", 2560, NULL, 5,
                          NULL, 1);
}

void battery_level_read(battery_state *out_state) {
  if (!input_battery_initialized)
    return;

  const int sampleCount = 8;
  int total_voltage_mv = 0;

  for (int i = 0; i < sampleCount; ++i) {
    int raw, voltage_mv;
    adc_oneshot_read(adc1_handle, ADC_PIN, &raw);

    if (do_calibration) {
      adc_cali_raw_to_voltage(adc1_cali_handle, raw, &voltage_mv);
    } else {
      voltage_mv = (raw * 3300) / 4095;
    }
    total_voltage_mv += voltage_mv;
  }

  float adcSample = (total_voltage_mv / (float)sampleCount) / 1000.0f;

  // Rolling average filter
  if (adc_value == 0.0f)
    adc_value = adcSample;
  else
    adc_value = (adc_value * 0.9f) + (adcSample * 0.1f);

  // Voltage Divider Calculation (Assuming R1=100k, R2=100k)
  const float Vs =
      (forced_adc_value > 0.0f) ? forced_adc_value : (adc_value * 2.0f);

  const float FullVoltage = 4.1f;
  const float EmptyVoltage = 3.4f;

  out_state->millivolts = (int)(Vs * 1000);
  out_state->percentage =
      (int)((Vs - EmptyVoltage) / (FullVoltage - EmptyVoltage) * 100.0f);

  if (out_state->percentage > 100)
    out_state->percentage = 100;
  if (out_state->percentage < 0)
    out_state->percentage = 0;

  out_state->state = getChargeStatus();
}

void battery_level_force_voltage(float volts) { forced_adc_value = volts; }

void battery_monitor_enabled_set(int value) {
  battery_monitor_enabled = (bool)value;
}
