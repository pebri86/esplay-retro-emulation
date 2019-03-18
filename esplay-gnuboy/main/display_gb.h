#ifndef _DISPLAY_GB_H_
#define _DISPLAY_GB_H_
#include <stdint.h>

#define GB_FRAME_WIDTH 160
#define GB_FRAME_HEIGHT 144

void write_gb_frame(const uint16_t *data, bool scale);

#endif /* _DISPLAY_GB_H_ */