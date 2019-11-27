#pragma once

#include <stdint.h>

typedef enum {
	/// Event to react to keypad presses
	EVENT_TYPE_KEYPAD,
	/// Audio player events
	EVENT_TYPE_AUDIO_PLAYER,
	/// Update screen, mainly used in simulation
	EVENT_TYPE_UPDATE,
	/// Close program, mainily used in simulation
	EVENT_TYPE_QUIT,
} event_type_t;

typedef struct {
	event_type_t type;
} event_head_t;

typedef struct {
	event_head_t head;
	uint16_t state;
	uint16_t pressed;
	uint16_t released;
} event_keypad_t;

typedef enum AudioPlayerEvent {
	AudioPlayerEventDone,
	AudioPlayerEventStateChanged,
	AudioPlayerEventError,
} AudioPlayerEvent;

typedef struct {
	event_head_t head;
	AudioPlayerEvent event;
} event_audio_player_t;

typedef union {
	event_type_t type;
	event_keypad_t keypad;
	event_audio_player_t audio_player;
} event_t;

void event_init(void);
int wait_event(event_t *event);
int push_event(event_t *event);
