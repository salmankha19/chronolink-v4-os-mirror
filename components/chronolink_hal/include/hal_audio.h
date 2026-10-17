/*
 * hal_audio.h
 *
 * ChronoLink V4 OS — Audio HAL
 *
 * Copyright (c) 2026 ChronoLink Project
 */
#pragma once
#include "hal.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t HAL_Audio_Init(void);
hal_status_t HAL_Audio_Deinit(void);

/* Play a 16-bit mono PCM buffer (blocking). The I2S channel is
   configured for mono; a stereo buffer is not accepted. */
hal_status_t HAL_Audio_Play(const int16_t *pcm, size_t samples);

/* Request a stop of any in-flight HAL_Audio_Play() from another task.
   Returns HAL_OK if a stop was requested, HAL_ERR_INIT if not initialised. */
hal_status_t HAL_Audio_Stop(void);

/* Set master volume 0..100 */
hal_status_t HAL_Audio_SetVolume(uint8_t vol);

#ifdef __cplusplus
}
#endif