#include "pet_motion.h"
#include <string.h>

static int32_t absolute(int32_t value) { return value < 0 ? -value : value; }

void pet_motion_init(PetMotion *m) { memset(m, 0, sizeof(*m)); }

uint8_t pet_motion_sample(PetMotion *m, const int32_t a[3],
                           const int32_t g[3], uint8_t valid, uint32_t now)
{
    int32_t linear[3], peak = 0, rotation = 0, norm = 0;
    uint32_t dt, duration;
    unsigned i;
    uint8_t event = PET_MOTION_NONE;
    if (!valid || !a || !g) { pet_motion_init(m); return event; }
    /* Reject corrupt/out-of-range transport data before integer arithmetic. */
    for (i = 0; i < 3; i++)
        if (a[i] < -8500 || a[i] > 8500 || g[i] < -600000 || g[i] > 600000) {
            pet_motion_init(m); return event;
        }
    dt = (uint32_t)(now - m->last_at);
    if (!m->initialized || dt > 120U) {
        pet_motion_init(m);
        for (i = 0; i < 3; i++) m->gravity[i] = a[i] * 64;
        m->initialized = 1; m->last_at = now;
        return event;
    }
    if (!dt) return event;
    m->last_at = now;
    for (i = 0; i < 3; i++) {
        linear[i] = a[i] - m->gravity[i] / 64;
        if (absolute(linear[i]) > peak) peak = absolute(linear[i]);
        if (absolute(g[i]) > rotation) rotation = absolute(g[i]);
        norm += a[i] * a[i];
        m->gravity[i] += (a[i] * 64 - m->gravity[i]) * (int32_t)dt / (320 + (int32_t)dt);
    }
    m->linear_peak_mg = (uint16_t)peak;
    if (peak <= 100 && rotation <= 15000 && norm >= 810000 && norm <= 1210000) {
        if (!m->quiet) { m->quiet = 1; m->quiet_at = now; }
        duration = (uint32_t)(now - m->quiet_at);
        m->stable = duration >= 400U;
        if (m->armed && m->candidate && duration >= 400U && !m->emitted) {
            event = PET_MOTION_LIGHT; m->emitted = 1; m->armed = 0;
        }
        if (duration >= 1000U) {
            m->armed = 1; m->candidate = 0; m->emitted = 0;
            memset(m->axis, 0, sizeof(m->axis));
        }
    } else { m->quiet = 0; m->stable = 0; }
    if (!m->armed || m->quiet) return event;
    for (i = 0; i < 3; i++) {
        PetMotionAxis *axis = &m->axis[i];
        int8_t sign;
        if (absolute(linear[i]) < PET_MOTION_LIGHT_MG) continue;
        sign = linear[i] > 0 ? 1 : -1;
        if (!axis->lobes || (uint32_t)(now - axis->first) > 1800U) {
            memset(axis, 0, sizeof(*axis));
            axis->sign = sign; axis->first = axis->last = now; axis->lobes = 1;
        } else if (axis->sign != sign && (uint32_t)(now - axis->last) >= 80U) {
            axis->sign = sign; axis->last = now; axis->lobe_heavy = 0;
            if (axis->lobes < 255) axis->lobes++;
        }
        if (axis->sign != sign) continue;
        /* Wrist rotation is common during gentle rocking. Angular speed alone
         * must not turn that gesture into a penalty; require strong linear
         * motion on at least two lobes of the same axis. */
        if (!axis->lobe_heavy && absolute(linear[i]) >= PET_MOTION_HEAVY_MG) {
            axis->lobe_heavy = 1;
            if (axis->heavy_lobes < 255) axis->heavy_lobes++;
        }
        if (axis->lobes >= 4 && (uint32_t)(now - axis->first) >= 300U) {
            m->candidate = 1;
            if (axis->heavy_lobes >= 2) {
                m->armed = 0; m->emitted = 1;
                return PET_MOTION_HEAVY;
            }
        }
    }
    return event;
}
