#include "../include/edgi_spec_features.h"

void edgi_spec_row_features(const uint8_t spec[40][40], float out[40])
{
    float prev = 0.f;

    for (int r = 0; r < 40; r++)
    {
        uint32_t s = 0;
        for (int c = 0; c < 40; c++)
            s += spec[r][c];
        float mean = (float)s * (1.0f / (40.0f * 255.0f));
        out[r] = mean - prev;
        prev = mean;
    }
}
