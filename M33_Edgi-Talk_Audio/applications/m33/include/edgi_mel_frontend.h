#ifndef EDGI_MEL_FRONTEND_H
#define EDGI_MEL_FRONTEND_H

#include "edgi_mel_config.h"
#include <stdint.h>

void edgi_mel_ring_reset(void);
void edgi_mel_ring_push(const int16_t *pcm, unsigned nsamples);
unsigned edgi_mel_ring_fill(void);

/** 将最近 1 s PCM 转为 log-mel [N_FRAMES][N_BANDS] 行主序 flat 或二维 */
int edgi_mel_compute(float out[EDGI_MEL_N_FRAMES][EDGI_MEL_N_BANDS]);

#endif /* EDGI_MEL_FRONTEND_H */
