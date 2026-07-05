#include "../include/edgi_mel_frontend.h"
#include "../include/edgi_mel_filterbank.h"
#include <math.h>
#include <rtthread.h>
#include <string.h>

static int16_t s_pcm_ring[EDGI_MEL_CLIP_SAMPLES];
static unsigned s_pcm_count;

typedef struct
{
    float r;
    float i;
} edgi_cplx_t;

static edgi_cplx_t s_fft_buf[512];
static float s_frame512[512];
static float s_power257[257];

void edgi_mel_ring_reset(void)
{
    s_pcm_count = 0;
    rt_memset(s_pcm_ring, 0, sizeof(s_pcm_ring));
}

void edgi_mel_ring_push(const int16_t *pcm, unsigned nsamples)
{
    unsigned i;

    if (nsamples == 0u || pcm == RT_NULL)
        return;

    if (nsamples >= EDGI_MEL_CLIP_SAMPLES)
    {
        rt_memcpy(s_pcm_ring, pcm + nsamples - EDGI_MEL_CLIP_SAMPLES,
                  EDGI_MEL_CLIP_SAMPLES * sizeof(int16_t));
        s_pcm_count = EDGI_MEL_CLIP_SAMPLES;
        return;
    }

    if (s_pcm_count + nsamples > EDGI_MEL_CLIP_SAMPLES)
    {
        unsigned drop = s_pcm_count + nsamples - EDGI_MEL_CLIP_SAMPLES;
        if (drop >= s_pcm_count)
            s_pcm_count = 0;
        else
        {
            rt_memmove(s_pcm_ring, s_pcm_ring + drop,
                       (s_pcm_count - drop) * sizeof(int16_t));
            s_pcm_count -= drop;
        }
    }

    for (i = 0; i < nsamples; i++)
        s_pcm_ring[s_pcm_count + i] = pcm[i];
    s_pcm_count += nsamples;
}

unsigned edgi_mel_ring_fill(void)
{
    return s_pcm_count;
}

static unsigned edgi_bit_reverse(unsigned x, unsigned bits)
{
    unsigned y = 0;
    unsigned i;
    for (i = 0; i < bits; i++)
    {
        y = (y << 1) | (x & 1u);
        x >>= 1;
    }
    return y;
}

static void edgi_fft512(edgi_cplx_t *a)
{
    const unsigned n = 512u;
    const unsigned bits = 9u;
    unsigned i, len, half, j, k;
    float ang, wpr, wpi, wr, wi, tr, ti;

    for (i = 0; i < n; i++)
    {
        unsigned jrev = edgi_bit_reverse(i, bits);
        if (jrev > i)
        {
            edgi_cplx_t t = a[i];
            a[i] = a[jrev];
            a[jrev] = t;
        }
    }

    for (len = 2; len <= n; len <<= 1)
    {
        ang = -6.28318530718f / (float)len;
        wpr = cosf(ang);
        wpi = sinf(ang);
        for (i = 0; i < n; i += len)
        {
            wr = 1.f;
            wi = 0.f;
            half = len >> 1;
            for (j = 0; j < half; j++)
            {
                k = i + j + half;
                tr = wr * a[k].r - wi * a[k].i;
                ti = wr * a[k].i + wi * a[k].r;
                a[k].r = a[i + j].r - tr;
                a[k].i = a[i + j].i - ti;
                a[i + j].r += tr;
                a[i + j].i += ti;
                tr = wr;
                wr = tr * wpr - wi * wpi;
                wi = tr * wpi + wi * wpr;
            }
        }
    }
}

static void edgi_frame_power(const float *frame512, float *power257)
{
    unsigned k;

    for (k = 0; k < 512u; k++)
    {
        s_fft_buf[k].r = frame512[k];
        s_fft_buf[k].i = 0.f;
    }
    edgi_fft512(s_fft_buf);
    for (k = 0; k < 257u; k++)
        power257[k] = s_fft_buf[k].r * s_fft_buf[k].r + s_fft_buf[k].i * s_fft_buf[k].i;
}

int edgi_mel_compute(float out[EDGI_MEL_N_FRAMES][EDGI_MEL_N_BANDS])
{
    unsigned pos = 0;
    unsigned f;
    float mean = 0.f;

    if (s_pcm_count < EDGI_MEL_CLIP_SAMPLES)
    {
        rt_memset(out, 0, EDGI_MEL_N_FRAMES * EDGI_MEL_N_BANDS * sizeof(float));
        return -1;
    }

    for (f = 0; f < EDGI_MEL_N_FRAMES; f++)
    {
        unsigned b, k;
        if (pos + EDGI_MEL_WIN_SAMPLES > EDGI_MEL_CLIP_SAMPLES)
        {
            rt_memset(out[f], 0, EDGI_MEL_N_BANDS * sizeof(float));
            continue;
        }

        rt_memset(s_frame512, 0, sizeof(s_frame512));
        for (b = 0; b < EDGI_MEL_WIN_SAMPLES; b++)
        {
            float s = (float)s_pcm_ring[pos + b] / 32768.f;
            s_frame512[b] = s * edgi_mel_hann[b];
        }

        edgi_frame_power(s_frame512, s_power257);
        for (b = 0; b < EDGI_MEL_N_BANDS; b++)
        {
            float acc = 0.f;
            const float *fb = &edgi_mel_filterbank[b * 257u];
            for (k = 0; k < 257u; k++)
                acc += fb[k] * s_power257[k];
            out[f][b] = logf(acc + 1e-6f);
        }
        pos += EDGI_MEL_HOP_SAMPLES;
    }

    for (f = 0; f < EDGI_MEL_N_FRAMES; f++)
    {
        unsigned b;
        for (b = 0; b < EDGI_MEL_N_BANDS; b++)
            mean += out[f][b];
    }
    mean /= (float)(EDGI_MEL_N_FRAMES * EDGI_MEL_N_BANDS);
    for (f = 0; f < EDGI_MEL_N_FRAMES; f++)
    {
        unsigned b;
        for (b = 0; b < EDGI_MEL_N_BANDS; b++)
            out[f][b] -= mean;
    }

    return 0;
}
