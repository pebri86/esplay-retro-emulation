#ifndef AUDIO_H
#define AUDIO_H

#define I2S_NUM I2S_NUM_0
#define VOLUME_LEVEL_COUNT (100)

void audio_init(int sample_rate);
void audio_submit(short *stereoAudioBuffer, int frameCount);
void audio_terminate();
void audio_resume();
int audio_volume_get();
void audio_volume_set(int value);
void audio_amp_enable();
void audio_amp_disable();

#endif
