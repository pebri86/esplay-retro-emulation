#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <stdint.h>
#include <stdbool.h>

enum { KEYPAD_START = 1,
       KEYPAD_SELECT = 2,
       KEYPAD_UP = 4,
       KEYPAD_DOWN = 8,
       KEYPAD_LEFT = 16,
       KEYPAD_RIGHT = 32,
       KEYPAD_A = 64,
       KEYPAD_B = 128,
       KEYPAD_MENU = 256,
       KEYPAD_L = 512,
       KEYPAD_R = 1024,
};

enum
{
    GAMEPAD_INPUT_START = 0,
    GAMEPAD_INPUT_SELECT,
    GAMEPAD_INPUT_UP,
    GAMEPAD_INPUT_DOWN,
    GAMEPAD_INPUT_LEFT,
    GAMEPAD_INPUT_RIGHT,
    GAMEPAD_INPUT_A,
    GAMEPAD_INPUT_B,
    GAMEPAD_INPUT_MENU,
    GAMEPAD_INPUT_L,
    GAMEPAD_INPUT_R,

    GAMEPAD_INPUT_MAX
};

typedef struct
{
    uint8_t values[GAMEPAD_INPUT_MAX];
} input_gamepad_state;

void gamepad_init();
void input_gamepad_terminate();
void gamepad_read(input_gamepad_state *out_state);
input_gamepad_state gamepad_input_read_raw();

uint16_t keypad_sample(void);
uint16_t keypad_debounce(uint16_t sample, uint16_t *changes);


#endif
