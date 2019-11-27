#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "display.h"
#include "hourglass_empty_black_48dp.h"
#include "splash.h"

#define LINE_BUFFERS (2)
#define LINE_COUNT (8)

uint16_t *line[LINE_BUFFERS];
extern uint16_t myPalette[];

void set_display_brightness(int percent)
{
#if (CONFIG_HW_LCD_TYPE == LCD_TYPE_ILI9342)
    ili9342_backlight_percentage_set(percent);
#else
    ili9341_backlight_percentage_set(percent);
#endif
}

void backlight_deinit()
{

#if (CONFIG_HW_LCD_TYPE == LCD_TYPE_ILI9342)
    ili9342_backlight_deinit();
#else
    ili9341_backlight_deinit();
#endif
}

void display_prepare(int percent)
{
#if (CONFIG_HW_LCD_TYPE == LCD_TYPE_ILI9342)
    ili9342_prepare();
#else
    ili9341_prepare();
#endif
}

void display_poweroff(int percent)
{
#if (CONFIG_HW_LCD_TYPE == LCD_TYPE_ILI9342)
    ili9342_poweroff();
#else
    ili9341_poweroff();
#endif
}

void write_frame_rectangleLE(short left, short top, short width, short height, uint16_t *buffer)
{
    short x, y, xv, yv;
    int sending_line = -1;
    int calc_line = 0;
    if (left < 0 )
        left = 0;
    if (top < 0)
        top = 0;
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    if (buffer == NULL)
    {
        for (y = top; y < height + top; y++)
        {
            xv = 0;
            for (x = left; x < width + left; x++)
            {
                line[calc_line][xv] = 0;
                xv++;
            }

            if (sending_line != -1)
                send_line_finish();
            sending_line = calc_line;
            calc_line = (calc_line == 1) ? 0 : 1;
            send_lines_ext(y, left, width, line[sending_line], 1);
        }

        send_line_finish();
    }
    else
    {
        yv = 0;
        for (y = top; y < top + height; y++)
        {
            xv = 0;
            for (int i = left; i < left + width; ++i)
            {
                uint16_t pixel = buffer[yv * width + xv];
                line[calc_line][xv] = ((pixel << 8) | (pixel >> 8));
                xv++;
            }

            if (sending_line != -1)
                send_line_finish();
            sending_line = calc_line;
            calc_line = (calc_line == 1) ? 0 : 1;
            send_lines_ext(y, left, width, line[sending_line], 1);
            yv++;
        }
    }

    send_line_finish();
}

void renderGfx(short left, short top, short width, short height, uint16_t *buffer, short sx, short sy, short tileSetWidth)
{
    short x, y, xv, yv;
    int sending_line = -1;
    int calc_line = 0;

    if (left < 0 )
        left = 0;
    if (top < 0)
        top = 0;
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    if (buffer == NULL)
    {
        for (y = top; y < height + top; y++)
        {
            xv = 0;
            for (x = left; x < width + left; x++)
            {
                line[calc_line][xv] = 0;
                xv++;
            }

            if (sending_line != -1)
                send_line_finish();
            sending_line = calc_line;
            calc_line = (calc_line == 1) ? 0 : 1;
            send_lines_ext(y, left, width, line[sending_line], 1);
        }

        send_line_finish();
    }
    else
    {
        yv = 0;
        for (y = top; y < top + height; y++)
        {
            xv = 0;
            for (int i = left; i < left + width; ++i)
            {
                uint16_t pixel = buffer[(yv + sy) * tileSetWidth + (xv + sx)];
                line[calc_line][xv] = ((pixel << 8) | (pixel >> 8));
                xv++;
            }

            if (sending_line != -1)
                send_line_finish();
            sending_line = calc_line;
            calc_line = (calc_line == 1) ? 0 : 1;
            send_lines_ext(y, left, width, line[sending_line], 1);
            yv++;
        }
    }

    send_line_finish();
}

void display_show_hourglass()
{
    write_frame_rectangleLE((LCD_WIDTH / 2) - (image_hourglass_empty_black_48dp.width / 2),
                            (LCD_HEIGHT / 2) - (image_hourglass_empty_black_48dp.height / 2),
                            image_hourglass_empty_black_48dp.width,
                            image_hourglass_empty_black_48dp.height,
                            image_hourglass_empty_black_48dp.pixel_data);
}

void display_show_splash()
{
    display_clear(0xffff);
    for (short i = 1; i < 151; ++i)
    {
        renderGfx((LCD_WIDTH - splash_screen.width) / 2,
                  (LCD_HEIGHT - splash_screen.height) / 2,
                  i,
                  splash_screen.height,
                  splash_screen.pixel_data,
                  0,
                  0,
                  150);
        vTaskDelay(2);
    }
    vTaskDelay(100);
}

const static uint8_t batEmptyIcon[]={
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
	1,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
	1,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,
	1,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
	1,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
	1,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
	1,0,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
	1,0,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,
	1,0,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0
};

void display_show_empty_battery() {
	const int cols[2][3]={
		{0x0000, 0xffff, 0xf800},
		{0x0000, 0xffff, 0x0000},
	};
    uint16_t buffer[32*12];
	for (int i=0; i<6; i++) {
		const uint8_t *p=batEmptyIcon;
		for (int y=0; y<12; y++) {
			for (int x=0; x<32; x++) {
                buffer[y*32+x] = cols[i&1][*p++];
			}
		}
        write_frame_rectangleLE((320-32)/2,(240-12)/2, 32, 12, buffer);
		vTaskDelay(500/portTICK_RATE_MS);
	}
}

void display_clear(uint16_t color)
{
    int sending_line = -1;
    int calc_line = 0;
    // clear the buffer
    for (int i = 0; i < LINE_BUFFERS; ++i)
    {
        for (int j = 0; j < LCD_WIDTH * LINE_COUNT; ++j)
        {
            line[i][j] = color;
        }
    }

    for (int y = 0; y < LCD_HEIGHT; y += LINE_COUNT)
    {
        if (sending_line != -1)
            send_line_finish();
        sending_line = calc_line;
        calc_line = (calc_line == 1) ? 0 : 1;
        send_lines_ext(y, 0, LCD_WIDTH, line[sending_line], LINE_COUNT);
    }

    send_line_finish();
}

void display_init()
{
    // Line buffers
    const size_t lineSize = LCD_WIDTH * LINE_COUNT * sizeof(uint16_t);
    for (int x = 0; x < LINE_BUFFERS; x++)
    {
        line[x] = heap_caps_malloc(lineSize, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!line[x])
            abort();
    }
    // Initialize the LCD
    disp_spi_init();
#if (CONFIG_HW_LCD_TYPE == LCD_TYPE_ILI9342)
    ili9342_init();
#else
    ili9341_init();
#endif
}
