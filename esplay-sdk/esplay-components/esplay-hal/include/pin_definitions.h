#ifndef PIN_DEFINITIONS_H
#define PIN_DEFINITIONS_H

#include <driver/adc.h>
#include "sdkconfig.h"

// TFT
#define DISP_SPI_MOSI 23
#define DISP_SPI_CLK 18
#define DISP_SPI_CS 5
#define DISP_SPI_DC 12
#define DISP_BCKL 27

// KEYPAD
#ifdef CONFIG_ESPLAY20_HW
#define A 32
#define B 33
#define START 36
#define SELECT 0
#define IO_Y ADC1_CHANNEL_7
#define IO_X ADC1_CHANNEL_6
#define MENU 13
#endif

#ifdef CONFIG_ESPLAY_MICRO_HW
#define L_BTN   36
#define R_BTN   34
#define MENU    35
#define I2C_SDA 21
#define I2C_SCL 22
#define I2C_ADDR 0x20
#endif

// STATUS LED
#define LED1 13

// AUDIO
#define I2S_BCK 26
#define I2S_WS 25
#define I2S_DOUT 19
#define AMP_SHDN 4

// POWER
#define USB_PLUG_PIN 32
#define CHRG_STATE_PIN 33
#define ADC_PIN ADC1_CHANNEL_3

#endif // PIN_DEFINITIONS_H
