#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display_gb.h"
#include "display.h"
#include "disp_spi.h"

extern uint16_t *line[];

#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define LINE_COUNT (4)

/* Resize using bilinear interpolation */
static uint16_t getPixel(const uint16_t *bufs, int x, int y, int w1, int h1, int w2, int h2)
{
    int x_diff, y_diff, xv, yv, red, green, blue, col, a, b, c, d, index;
    int x_ratio = (int)(((w1 - 1) << 16) / w2) + 1;
    int y_ratio = (int)(((h1 - 1) << 16) / h2) + 1;

    xv = (int)((x_ratio * x) >> 16);
    yv = (int)((y_ratio * y) >> 16);

    x_diff = ((x_ratio * x) >> 16) - (xv);
    y_diff = ((y_ratio * y) >> 16) - (yv);

    index = yv * w1 + xv;

    a = bufs[index];
    b = bufs[index + 1];
    c = bufs[index + w1];
    d = bufs[index + w1 + 1];

    red = (((a >> 11) & 0x1f) * (1 - x_diff) * (1 - y_diff) + ((b >> 11) & 0x1f) * (x_diff) * (1 - y_diff) +
           ((c >> 11) & 0x1f) * (y_diff) * (1 - x_diff) + ((d >> 11) & 0x1f) * (x_diff * y_diff));

    green = (((a >> 5) & 0x3f) * (1 - x_diff) * (1 - y_diff) + ((b >> 5) & 0x3f) * (x_diff) * (1 - y_diff) +
             ((c >> 5) & 0x3f) * (y_diff) * (1 - x_diff) + ((d >> 5) & 0x3f) * (x_diff * y_diff));

    blue = (((a)&0x1f) * (1 - x_diff) * (1 - y_diff) + ((b)&0x1f) * (x_diff) * (1 - y_diff) +
            ((c)&0x1f) * (y_diff) * (1 - x_diff) + ((d)&0x1f) * (x_diff * y_diff));

    col = ((int)red << 11) | ((int)green << 5) | ((int)blue);

    return col;
}

void write_gb_frame(const uint16_t *data, bool scale)
{
    short x, y;
    int sending_line = -1;
    int calc_line = 0;

    if (data == NULL)
    {
        for (y = 0; y < LCD_HEIGHT; ++y)
        {
            for (x = 0; x < LCD_WIDTH; x++)
            {
                line[calc_line][x] = 0;
            }
            if (sending_line != -1)
                send_line_finish();
            sending_line = calc_line;
            calc_line = (calc_line == 1) ? 0 : 1;
            send_lines_ext(y, 0, LCD_WIDTH, line[sending_line], 1);
        }
        send_line_finish();
    }
    else
    {
        if (scale)
        {
            int outputHeight = LCD_HEIGHT;
            int outputWidth = GB_FRAME_WIDTH + (LCD_HEIGHT - GB_FRAME_HEIGHT);
            int xpos = (LCD_WIDTH - outputWidth) / 2;

            for (y = 0; y < outputHeight; y += LINE_COUNT)
            {
                for (int i = 0; i < LINE_COUNT; ++i)
                {
                    if ((y + i) >= outputHeight)
                        break;

                    int index = (i)*outputWidth;

                    for (x = 0; x < outputWidth; ++x)
                    {
                        uint16_t sample = getPixel(data, x, (y + i), GB_FRAME_WIDTH, GB_FRAME_HEIGHT, outputWidth, outputHeight);
                        line[calc_line][index++] = ((sample >> 8) | ((sample) << 8));
                    }
                }
                if (sending_line != -1)
                    send_line_finish();
                sending_line = calc_line;
                calc_line = (calc_line == 1) ? 0 : 1;
                send_lines_ext(y, xpos, outputWidth, line[sending_line], LINE_COUNT);
            }
            send_line_finish();
        }
        else
        {
            int ypos = (LCD_HEIGHT - GB_FRAME_HEIGHT) / 2;
            int xpos = (LCD_WIDTH - GB_FRAME_WIDTH) / 2;

            for (y = 0; y < GB_FRAME_HEIGHT; y += LINE_COUNT)
            {
                for (int i = 0; i < LINE_COUNT; ++i)
                {
                    if ((y + i) >= GB_FRAME_HEIGHT)
                        break;

                    int index = (i)*GB_FRAME_WIDTH;
                    int bufferIndex = ((y + i) * GB_FRAME_WIDTH);

                    for (x = 0; x < GB_FRAME_WIDTH; ++x)
                    {
                        uint16_t sample = data[bufferIndex++];
                        line[calc_line][index++] = ((sample >> 8) | ((sample & 0xff) << 8));
                    }
                }
                if (sending_line != -1)
                    send_line_finish();
                sending_line = calc_line;
                calc_line = (calc_line == 1) ? 0 : 1;
                send_lines_ext(y + ypos, xpos, GB_FRAME_WIDTH, line[sending_line], LINE_COUNT);
            }
            send_line_finish();
        }
    }
}