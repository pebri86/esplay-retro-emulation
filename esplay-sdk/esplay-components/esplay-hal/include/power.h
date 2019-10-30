#pragma once

typedef struct
{
	int millivolts;
	int percentage;
} battery_state;

typedef enum
{
  NO_CHRG = 0,
	CHARGING,
	FULL_CHARGED
} charging_state;

void system_sleep();
void esplay_system_init();
void battery_level_init();
void battery_level_read(battery_state *out_state);
void battery_level_force_voltage(float volts);
void battery_monitor_enabled_set(int value);
charging_state getChargeStatus();
void system_led_set(int state);