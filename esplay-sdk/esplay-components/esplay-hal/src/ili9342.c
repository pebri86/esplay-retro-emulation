/**
 * @file ili9342.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ili9342.h"
#include "disp_spi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "lcd_struct.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "pin_definitions.h"

/*********************
 *      DEFINES
 *********************/
#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_ML 0x10
#define MADCTL_MH 0x04
#define TFT_RGB_BGR 0x08

#define TFT_CMD_SWRESET 0x01
#define TFT_CMD_SLEEP 0x10
#define TFT_CMD_DISPLAY_OFF 0x28
/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void ili9342_send_cmd(uint8_t cmd);
static void ili9342_send_data(void *data, uint16_t length);
static void backlight_init();

/**********************
 *  STATIC VARIABLES
 **********************/
static const int DUTY_MAX = 0x1fff;
static const int LCD_BACKLIGHT_ON_VALUE = 1;
static bool isBackLightIntialized = false;

DRAM_ATTR static const lcd_init_cmd_t ili_sleep_cmds[] = {
    {TFT_CMD_SWRESET, {0}, 0x80},
    {TFT_CMD_DISPLAY_OFF, {0}, 0x80},
    {TFT_CMD_SLEEP, {0}, 0x80},
    {0, {0}, 0xff}
};

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ili9342_init(void)
{
	lcd_init_cmd_t ili_init_cmds[] = {
		{TFT_CMD_SWRESET, {0}, 0x80},
	    {0xCF, {0x00, 0xc3, 0x30}, 3},
	    {0xED, {0x64, 0x03, 0x12, 0x81}, 4},
	    {0xE8, {0x85, 0x00, 0x78}, 3},
	    {0xCB, {0x39, 0x2c, 0x00, 0x34, 0x02}, 5},
	    {0xF7, {0x20}, 1},
	    {0xEA, {0x00, 0x00}, 2},
	    {0xC0, {0x1B}, 1},    //Power control   //VRH[5:0]
	    {0xC1, {0x12}, 1},    //Power control   //SAP[2:0];BT[3:0]
	    {0xC5, {0x32, 0x3C}, 2},    //VCM control
	    {0x36, {(MADCTL_MV | MADCTL_MY | TFT_RGB_BGR)}, 1},    // Memory Access Control
	    {0x3A, {0x55}, 1},
	    {0xB1, {0x00, 0x1B}, 2},  // Frame Rate Control (1B=70, 1F=61, 10=119)
	    {0xB6, {0x0A, 0xA2}, 2},    // Display Function Control
	    {0xF6, {0x01, 0x30}, 2},
	    {0xF2, {0x00}, 1},    // 3Gamma Function Disable
	    {0x26, {0x01}, 1},     //Gamma curve selected

	    //Set Gamma
	    {0xE0, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15},
	    {0XE1, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15},

	    // ILI9342 Specific
	    {0x36, {0x40|0x80|0x08}, 1}, // <-- ROTATE
	    {0x21, {0}, 0x80}, // <-- INVERT COLORS

	    {0x11, {0}, 0x80},    //Exit Sleep
	    {0x29, {0}, 0x80},    //Display on
	    {0, {0}, 0xff}
	};

	//Initialize non-SPI GPIOs
	gpio_set_direction(DISP_BCKL, GPIO_MODE_OUTPUT);

	//Reset the display
	ili9342_send_cmd(TFT_CMD_SWRESET);
	vTaskDelay(100 / portTICK_RATE_MS);

	printf("ILI9342 initialization.\n");

	//Send all the commands
	uint16_t cmd = 0;
	while (ili_init_cmds[cmd].databytes != 0xff)
	{
		ili9342_send_cmd(ili_init_cmds[cmd].cmd);
		ili9342_send_data(ili_init_cmds[cmd].data, ili_init_cmds[cmd].databytes & 0x1F);
		if (ili_init_cmds[cmd].databytes & 0x80)
		{
			vTaskDelay(100 / portTICK_RATE_MS);
		}
		cmd++;
	}

	///Enable backlight
	printf("Enable backlight.\n");
	backlight_init();
}

int ili9342_is_backlight_initialized()
{
	return isBackLightIntialized;
}

void ili9342_backlight_percentage_set(int value)
{
	int duty = DUTY_MAX * (value * 0.01f);

	ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty, 500);
	ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
}

void ili9342_poweroff()
{
	esp_err_t err = ESP_OK;

	// fade off backlight
	ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (LCD_BACKLIGHT_ON_VALUE) ? 0 : DUTY_MAX, 100);
	ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_WAIT_DONE);

	// Disable LCD panel
	int cmd = 0;
	while (ili_sleep_cmds[cmd].databytes != 0xff)
	{
		ili9342_send_cmd(ili_sleep_cmds[cmd].cmd);
		ili9342_send_data(ili_sleep_cmds[cmd].data, ili_sleep_cmds[cmd].databytes & 0x7f);
		if (ili_sleep_cmds[cmd].databytes & 0x80)
		{
			vTaskDelay(100 / portTICK_RATE_MS);
		}
		cmd++;
	}

	err = rtc_gpio_init(DISP_BCKL);
	if (err != ESP_OK)
	{
		abort();
	}

	err = rtc_gpio_set_direction(DISP_BCKL, RTC_GPIO_MODE_OUTPUT_ONLY);
	if (err != ESP_OK)
	{
		abort();
	}

	err = rtc_gpio_set_level(DISP_BCKL, LCD_BACKLIGHT_ON_VALUE ? 0 : 1);
	if (err != ESP_OK)
	{
		abort();
	}
}

void ili9342_prepare()
{
	// Return use of backlight pin
	esp_err_t err = rtc_gpio_deinit(DISP_BCKL);
	if (err != ESP_OK)
	{
		abort();
	}
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void ili9342_send_cmd(uint8_t cmd)
{
	disp_spi_send(&cmd, 1, CMD_ON);
}

static void ili9342_send_data(void *data, uint16_t length)
{
	disp_spi_send(data, length, DATA_ON);
}

void ili9342_backlight_deinit()
{
	ledc_fade_func_uninstall();
	esp_err_t err = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
	if (err != ESP_OK)
	{
		printf("%s: ledc_stop failed.\n", __func__);
	}
}

static void backlight_init()
{
	//configure timer0
	ledc_timer_config_t ledc_timer;
	memset(&ledc_timer, 0, sizeof(ledc_timer));

	ledc_timer.duty_resolution = LEDC_TIMER_13_BIT; //set timer counter bit number
	ledc_timer.freq_hz = 5000;						//set frequency of pwm
	ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;	//timer mode,
	ledc_timer.timer_num = LEDC_TIMER_0;			//timer index

	ledc_timer_config(&ledc_timer);

	//set the configuration
	ledc_channel_config_t ledc_channel;
	memset(&ledc_channel, 0, sizeof(ledc_channel));

	//set LEDC channel 0
	ledc_channel.channel = LEDC_CHANNEL_0;
	//set the duty for initialization.(duty range is 0 ~ ((2**duty_resolution)-1)
	ledc_channel.duty = (LCD_BACKLIGHT_ON_VALUE) ? 0 : DUTY_MAX;
	//GPIO number
	ledc_channel.gpio_num = DISP_BCKL;
	//GPIO INTR TYPE, as an example, we enable fade_end interrupt here.
	ledc_channel.intr_type = LEDC_INTR_FADE_END;
	//set LEDC mode, from ledc_mode_t
	ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
	//set LEDC timer source, if different channel use one timer,
	//the frequency and duty_resolution of these channels should be the same
	ledc_channel.timer_sel = LEDC_TIMER_0;

	ledc_channel_config(&ledc_channel);

	//initialize fade service.
	ledc_fade_func_install(0);

	// duty range is 0 ~ ((2**duty_resolution)-1)
	ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (LCD_BACKLIGHT_ON_VALUE) ? DUTY_MAX : 0, 500);
	ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);

	isBackLightIntialized = true;

	printf("Backlight initialization done.\n");
}