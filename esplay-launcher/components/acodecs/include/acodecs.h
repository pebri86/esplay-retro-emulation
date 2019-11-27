#pragma once

#include <stdint.h>
#include <stdbool.h>

/** The audiocodec to be decoded. */
typedef enum AudioCodec {
	AudioCodecUnknown,
	AudioCodecMP3,
	AudioCodecOGG,
	AudioCodecMOD,
	AudioCodecWAV,
	AudioCodecFLAC,
	AudioCodecGME,
} AudioCodec;

typedef struct AudioInfo {
	unsigned sample_rate;
	unsigned channels;
	unsigned buf_size;
} AudioInfo;

/** An AudioDecoder provides audio decoding functionality given a filename. */
typedef struct AudioDecoder {
	/** Open the given filename, initializing the given handle. */
	int (*open)(void **handle, const char *filename);
	/** Retreive audio information, given the handle. */
	int (*get_info)(void *handle, AudioInfo *info);
	/** Using the handle, decode buf_len samples and write them into buf. */
	int (*decode)(void *handle, int16_t *buf, int num_c, unsigned buf_len);
	/** Close the given handle, eventually freeing memory. */
	int (*close)(void *handle);
} AudioDecoder;

/** Choose an AudioDecoder given the codec and return it */
AudioDecoder *acodec_get_decoder(AudioCodec codec);
