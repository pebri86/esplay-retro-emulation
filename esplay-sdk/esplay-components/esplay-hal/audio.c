#include "audio.h"

#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "settings.h"
#include "pin_definitions.h"

static float Volume = 1.0f;
static int volumeLevel = 30;
static int sampleRate;

int audio_volume_get()
{
    return volumeLevel;
}

void audio_volume_set(int value)
{
    if (value > VOLUME_LEVEL_COUNT)
    {
        printf("audio_volume_set: value out of range (%d)\n", value);
        abort();
    }

    volumeLevel = value;
    Volume = (float)(volumeLevel*10) * 0.001f;

    if (volumeLevel == 0)
        audio_amp_disable();
}

void audio_init(int sample_rate)
{
    gpio_set_direction(AMP_SHDN, GPIO_MODE_OUTPUT);
    printf("%s: sample_rate=%d\n", __func__, sample_rate);

    // NOTE: buffer needs to be adjusted per AUDIO_SAMPLE_RATE
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX, // Only TX
        .sample_rate = sample_rate,
        .bits_per_sample = 16,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, //2-channels
        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
        .dma_buf_count = 8,
        //.dma_buf_len = 1472 / 2,  // (368samples * 2ch * 2(short)) = 1472
        .dma_buf_len = 534,                       // (416samples * 2ch * 2(short)) = 1664
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, //Interrupt level 1
        .use_apll = 1
        };

    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_DOUT,
        .data_in_num = -1 //Not used
    };
    i2s_set_pin(I2S_NUM, &pin_config);
    sampleRate = sample_rate;
    volumeLevel = get_volume_settings();
    if(volumeLevel != 0)
        audio_amp_enable();
}

void audio_submit(short *stereoAudioBuffer, int frameCount)
{
    if (volumeLevel != 0)
    {
        short currentAudioSampleCount = frameCount * 2;

        for (short i = 0; i < currentAudioSampleCount; ++i)
        {
            int sample = stereoAudioBuffer[i] * Volume;
            if (sample > 32767)
                sample = 32767;
            else if (sample < -32767)
                sample = -32767;

            stereoAudioBuffer[i] = (short)sample;
        }

        int len = currentAudioSampleCount * sizeof(int16_t);
        size_t count;
        i2s_write(I2S_NUM, (const char *)stereoAudioBuffer, len, &count, portMAX_DELAY);
        if (count != len)
        {
            printf("i2s_write_bytes: count (%d) != len (%d)\n", count, len);
            abort();
        }
    }
}

void audio_terminate()
{
    audio_amp_disable();
    i2s_zero_dma_buffer(I2S_NUM);
    i2s_stop(I2S_NUM);

    i2s_start(I2S_NUM);
}

void audio_resume()
{
    if (volumeLevel != 0)
        audio_amp_enable();
}

void audio_amp_enable()
{
    gpio_set_level(AMP_SHDN, 1);
}

void audio_amp_disable()
{
    gpio_set_level(AMP_SHDN, 0);
}