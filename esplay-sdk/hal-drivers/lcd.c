#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_periph.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "lcd.h"


static const char *TAG = "hal-lcd";

// Pin Configuration
#define LCD_MOSI 23
#define LCD_CLK 18
#define LCD_CS 5
#define LCD_DC 12
#define LCD_BCKL 27

// SPI Parameter
#define SPI_CLOCK_SPEED (80 * 1000 * 1000)
#define LCD_HOST VSPI_HOST

// TFT Parameter
#define CMD_ON 0
#define DATA_ON 1
#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_ML 0x10
#define MADCTL_MH 0x04
#define TFT_RGB_BGR 0x08

#define TFT_CMD_SWRESET 0x01
#define TFT_CMD_SLEEP 0x10
#define TFT_CMD_DISPLAY_OFF 0x28

static const int DUTY_MAX = 0x1fff;
static const int LCD_BACKLIGHT_ON_VALUE = 1;
static bool isBackLightIntialized = false;
static spi_device_handle_t spi;

typedef struct
{
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} lcd_init_cmd_t;

static void disp_spi_send(uint8_t *data, uint16_t length, int dc)
{
    if (length == 0)
        return; //no need to send anything

    spi_transaction_t t;
    memset(&t, 0, sizeof(t)); //Zero out the transaction
    t.length = length * 8;    //Length is in bytes, transaction length is in bits.
    t.tx_buffer = data;       //Data
    t.user = (void *)dc;

    spi_device_queue_trans(spi, &t, portMAX_DELAY);

    spi_transaction_t *rt;
    spi_device_get_trans_result(spi, &rt, portMAX_DELAY);
}
	
static void disp_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(LCD_DC, dc);
}
	
static void ili9341_send_cmd(uint8_t cmd)
{
	disp_spi_send(&cmd, 1, CMD_ON);
}

static void ili9341_send_data(void *data, uint16_t length)
{
	disp_spi_send(data, length, DATA_ON);
}

static void backlight_init()
{
    //configure timer0
    ledc_timer_config_t ledc_timer;
    memset(&ledc_timer, 0, sizeof(ledc_timer));

    ledc_timer.duty_resolution = LEDC_TIMER_13_BIT; //set timer counter bit number
    ledc_timer.freq_hz = 5000;                      //set frequency of pwm
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;    //timer mode,
    ledc_timer.timer_num = LEDC_TIMER_0;            //timer index

    ledc_timer_config(&ledc_timer);

    //set the configuration
    ledc_channel_config_t ledc_channel;
    memset(&ledc_channel, 0, sizeof(ledc_channel));

    //set LEDC channel 0
    ledc_channel.channel = LEDC_CHANNEL_0;
    //set the duty for initialization.(duty range is 0 ~ ((2**duty_resolution)-1)
    ledc_channel.duty = (LCD_BACKLIGHT_ON_VALUE) ? 0 : DUTY_MAX;
    //GPIO number
    ledc_channel.gpio_num = LCD_BCKL;
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

    ESP_LOGI(TAG, "Backlight initialization done.");
}

static void ili9341_init(void)
{
	lcd_init_cmd_t ili_init_cmds[] = {
		{TFT_CMD_SWRESET, {0}, 0x80},
		{0xCF, {0x00, 0x83, 0X30}, 3},
		{0xED, {0x64, 0x03, 0X12, 0X81}, 4},
		{0xE8, {0x85, 0x01, 0x79}, 3},
		{0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
		{0xF7, {0x20}, 1},
		{0xEA, {0x00, 0x00}, 2},
		{0xC0, {0x26}, 1},											  /*Power control*/
		{0xC1, {0x11}, 1},											  /*Power control */
		{0xC5, {0x35, 0x3E}, 2},									  /*VCOM control*/
		{0xC7, {0xBE}, 1},											  /*VCOM control*/
		{0x36, {MADCTL_MV | TFT_RGB_BGR}, 1}, /*Memory Access Control*/
		{0x3A, {0x55}, 1},											  /*Pixel Format Set*/
		{0xB1, {0x00, 0x1B}, 2},
		{0xF2, {0x08}, 1},
		{0x26, {0x01}, 1},
		{0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0X87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
		{0XE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
		{0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
		{0x2B, {0x00, 0x00, 0x01, 0x3f}, 4},
		{0x2C, {0}, 0},
		{0xB7, {0x07}, 1},
		{0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
		{0x11, {0}, 0x80},
		{0x29, {0}, 0x80},
		{0, {0}, 0xff},
	};

	//Reset the display
	ili9341_send_cmd(TFT_CMD_SWRESET);
	vTaskDelay(100 / portTICK_RATE_MS);

	printf("ILI9341 initialization.\n");

	//Send all the commands
	uint16_t cmd = 0;
	while (ili_init_cmds[cmd].databytes != 0xff)
	{
		ili9341_send_cmd(ili_init_cmds[cmd].cmd);
		ili9341_send_data(ili_init_cmds[cmd].data, ili_init_cmds[cmd].databytes & 0x1F);
		if (ili_init_cmds[cmd].databytes & 0x80)
		{
			vTaskDelay(100 / portTICK_RATE_MS);
		}
		cmd++;
	}
}

void set_brightness(int percent)
{
    int duty = DUTY_MAX * (percent * 0.01f);

    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty, 500);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
}

void backlight_deinit()
{
    ledc_fade_func_uninstall();
    esp_err_t err = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    if (err != ESP_OK)
    {
        printf("%s: ledc_stop failed.\n", __func__);
    }
}

void backlight_prepare()
{
    // Return use of backlight pin
    esp_err_t err = rtc_gpio_deinit(LCD_BCKL);
    if (err != ESP_OK)
    {
        abort();
    }
}

void backlight_poweroff()
{
    esp_err_t err = ESP_OK;

    // fade off backlight
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (LCD_BACKLIGHT_ON_VALUE) ? 0 : DUTY_MAX, 100);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_WAIT_DONE);

    err = rtc_gpio_init(LCD_BCKL);
    if (err != ESP_OK)
    {
        abort();
    }

    err = rtc_gpio_set_direction(LCD_BCKL, RTC_GPIO_MODE_OUTPUT_ONLY);
    if (err != ESP_OK)
    {
        abort();
    }

    err = rtc_gpio_set_level(LCD_BCKL, LCD_BACKLIGHT_ON_VALUE ? 0 : 1);
    if (err != ESP_OK)
    {
        abort();
    }
}

void lcd_draw(int x1, int y1, int x2, int y2, void *data)
{
    esp_err_t ret;
    int x;
    static spi_transaction_t trans[6];

    for (x = 0; x < 6; x++)
    {
        memset(&trans[x], 0, sizeof(spi_transaction_t));
        if ((x & 1) == 0)
        {
            //Even transfers are commands
            trans[x].length = 8;
            trans[x].user = (void *)0;
        }
        else
        {
            //Odd transfers are data
            trans[x].length = 8 * 4;
            trans[x].user = (void *)1;
        }
        trans[x].flags = SPI_TRANS_USE_TXDATA;
    }
    trans[0].tx_data[0] = 0x2A;                      //Column Address Set
    trans[1].tx_data[0] = (x1 >> 8) & 0xff;                 //Start Col High
    trans[1].tx_data[1] = x1 & 0xff;               //Start Col Low
    trans[1].tx_data[2] = ((x2 - 1) >> 8) & 0xff;   //End Col High
    trans[1].tx_data[3] = (x2 - 1) & 0xff; //End Col Low
    trans[2].tx_data[0] = 0x2B;                      //Page address set
    trans[3].tx_data[0] = (y1 >> 8) & 0xff;                 //Start page high
    trans[3].tx_data[1] = y1 & 0xff;               //start page low
    trans[3].tx_data[2] = ((y2 - 1) >> 8) & 0xff;   //end page high
    trans[3].tx_data[3] = (y2 - 1) & 0xff; //end page low
    trans[4].tx_data[0] = 0x2C;                      //memory write
    trans[5].tx_buffer = data;                   //finally send the line data
    trans[5].length = (x2-x1) * (y2-y1) * 2 * 8;     //Data length, in bits
    trans[5].flags = 0;                              //undo SPI_TRANS_USE_TXDATA flag

    //Queue all transactions.
    for (x = 0; x < 6; x++)
    {
        ret = spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
        assert(ret == ESP_OK);
    }
}

void wait_for_finish(void)
{
    spi_transaction_t *rtrans;
    esp_err_t ret;
    //Wait for all 6 transactions to be done and get back the results.
    for (int x = 0; x < 6; x++)
    {
        ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
        assert(ret == ESP_OK);
    }
}

void lcd_init()
{
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[LCD_DC], PIN_FUNC_GPIO);
    gpio_set_direction(LCD_DC, GPIO_MODE_OUTPUT);

    backlight_init();

    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_CLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 20 * LCD_H_RES * 2 + 8
    };
    
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_CLOCK_SPEED,
        .mode = 0,
        .spics_io_num = LCD_CS,
        .queue_size = 10,
        .pre_cb = disp_spi_pre_transfer_callback,
        .post_cb = NULL,
    };

    // Initialize the SPI bus
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, 1));
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &devcfg, &spi));
    
    set_brightness(0);
    ili9341_init();
    set_brightness(100);
}
