#pragma once

#include <stdbool.h>
#include <file_ops.h>

typedef struct AudioPlayerParam {
	Entry *entries;
	int n_entries;
	int index;
	const char *cwd;
	bool play_all; // true -> play all songs in folder
} AudioPlayerParam;

/** Start the audio player at the file entry at index. */
int audio_player(AudioPlayerParam param);
