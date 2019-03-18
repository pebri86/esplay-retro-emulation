#ifndef _DISPLAY_NES_H_
#define _DISPLAY_NES_H_
#include <stdint.h>

#define NES_FRAME_WIDTH 256
#define NES_FRAME_HEIGHT 224

void write_nes_frame(const uint8_t * data);

#endif /* _DISPLAY_NES_H_ */