#pragma once

// STATUS LED
#define LED1 13

// POWER
#define USB_PLUG_PIN 32
#define CHRG_STATE_PIN 33
#define ADC_PIN ADC_CHANNEL_3

typedef enum { NO_CHRG = 0, CHARGING, FULL_CHARGED } charging_state;

typedef struct {
  int millivolts;
  int percentage;
  charging_state state;
} battery_state;

void system_sleep();
void esplay_system_init();
void battery_level_init();
void battery_level_read(battery_state *out_state);
void battery_level_force_voltage(float volts);
void battery_monitor_enabled_set(int value);
charging_state getChargeStatus();
void system_led_set(int state);
