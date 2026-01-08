/**
 * @file lcd.c
 * @brief LCD display initialization implementation.
 *
 * Initializes the ILI9341 LCD panel via SPI and configures the backlight.
 */

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>

#include "lcd.h"

#define LCD_HOST SPI2_HOST
#define LCD_CS 5
#define LCD_DC 12
#define LCD_RST -1
#define LCD_BK_LIGHT 27

// Define PWM constants
#define LCD_LEDC_TIMER LEDC_TIMER_0
#define LCD_LEDC_MODE LEDC_LOW_SPEED_MODE
#define LCD_LEDC_CHANNEL LEDC_CHANNEL_0
#define LCD_LEDC_DUTY_RES LEDC_TIMER_10_BIT // 10-bit resolution (0-1023)
#define LCD_LEDC_FREQ_HZ 5000               // 5kHz frequency

/**
 * @brief Initialize the LCD backlight using PWM (LEDC).
 */
static void backlight_init() {
  // Prepare and then apply the LEDC PWM timer configuration
  ledc_timer_config_t ledc_timer = {.speed_mode = LCD_LEDC_MODE,
                                    .timer_num = LCD_LEDC_TIMER,
                                    .duty_resolution = LCD_LEDC_DUTY_RES,
                                    .freq_hz = LCD_LEDC_FREQ_HZ,
                                    .clk_cfg = LEDC_AUTO_CLK};
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

  // Prepare and then apply the LEDC PWM channel configuration
  ledc_channel_config_t ledc_channel = {
      .speed_mode = LCD_LEDC_MODE,
      .channel = LCD_LEDC_CHANNEL,
      .timer_sel = LCD_LEDC_TIMER,
      .intr_type = LEDC_INTR_DISABLE,
      .gpio_num = LCD_BK_LIGHT,
      .duty = 1023, // Start at max brightness (10-bit max)
      .hpoint = 0};
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

/**
 * @brief Set LCD brightness level.
 * * @param brightness Value from 0 to 100
 */
void lcd_set_brightness(uint8_t brightness) {
  if (brightness > 100)
    brightness = 100;

  // Map 0-100% to 0-1023 (for 10-bit resolution)
  uint32_t duty = (brightness * 1023) / 100;

  ESP_ERROR_CHECK(ledc_set_duty(LCD_LEDC_MODE, LCD_LEDC_CHANNEL, duty));
  ESP_ERROR_CHECK(ledc_update_duty(LCD_LEDC_MODE, LCD_LEDC_CHANNEL));
}

/**
 * @brief Initialize the LCD display.
 *
 * Sets up SPI bus, panel IO, and ILI9341 panel configuration.
 * Also initializes the backlight.
 *
 * @param panel Pointer to store the LCD panel handle.
 */
void lcd_init(esp_lcd_panel_handle_t *panel) {
  spi_bus_config_t buscfg = {
      .mosi_io_num = 23,
      .miso_io_num = -1,
      .sclk_io_num = 18,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = LCD_DC,
      .cs_gpio_num = LCD_CS,
      .pclk_hz = 40 * 1000 * 1000,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .spi_mode = 0,
      .trans_queue_depth = 10,
      .on_color_trans_done = NULL,
      .user_ctx = NULL,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                           &io_config, &io_handle));

  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = LCD_RST,
      .rgb_endian = LCD_RGB_ENDIAN_BGR,
      .bits_per_pixel = 16,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(*panel));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(*panel, true));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel, true));

  backlight_init();
}

void lcd_draw(esp_lcd_panel_handle_t panel, int x1, int y1, int x2, int y2,
              uint8_t *px_map) {
  esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, px_map);
}
