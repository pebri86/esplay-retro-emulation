#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display_nes.h"
#include "display.h"
#include "disp_spi.h"

extern uint16_t myPalette[];
extern uint16_t* line[];

#define LCD_WIDTH    320
#define LCD_HEIGHT   240
#define LINE_COUNT   (4)

void write_nes_frame(const uint8_t * data)
{
    short x,y;
    int sending_line=-1;
    int calc_line=0;

    if (data == NULL)
    {
        for (y=0; y<LCD_HEIGHT; ++y) {
            for (x=0; x<LCD_WIDTH; x++) {
                line[calc_line][x] = 0;
            }
            if (sending_line!=-1) send_line_finish();
            sending_line=calc_line;
            calc_line=(calc_line==1)?0:1;
            send_lines_ext(y, 0, LCD_WIDTH, line[sending_line], 1);
            }
        send_line_finish(); 
    }
    else
    {
        /* place output on center of lcd */
        int outputHeight = NES_FRAME_HEIGHT;
        int outputWidth = NES_FRAME_WIDTH;
        int xpos = (LCD_WIDTH - outputWidth) / 2;
        int ypos = (LCD_HEIGHT - outputHeight) / 2;

        for (y=ypos; y<outputHeight; y+=LINE_COUNT) 
        {
            for (int i = 0; i < LINE_COUNT; ++i)
            {
                if((y + i) >= outputHeight) break;

                int index = (i) * outputWidth;
                int bufferIndex = ((y + i) * NES_FRAME_WIDTH);

                for (x=0; x<outputWidth; x++)
                {
                    line[calc_line][index++] = myPalette[(unsigned char) (data[bufferIndex++])];
                }
            }
            if (sending_line!=-1) send_line_finish();
            sending_line=calc_line;
            calc_line=(calc_line==1)?0:1;
            send_lines_ext(y, xpos, outputWidth, line[sending_line], LINE_COUNT);
        }
        send_line_finish();
    }
}