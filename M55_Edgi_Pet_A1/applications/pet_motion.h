#ifndef PET_MOTION_H
#define PET_MOTION_H
#include <stdint.h>

enum { PET_MOTION_NONE, PET_MOTION_LIGHT, PET_MOTION_HEAVY };
#define PET_MOTION_LIGHT_MG 280
#define PET_MOTION_HEAVY_MG 1400

typedef struct {
    uint32_t first, last;
    int8_t sign;
    uint8_t lobes, heavy_lobes, lobe_heavy;
} PetMotionAxis;
typedef struct {
    int32_t gravity[3]; /* mg * 64, to avoid small-step integer dead zones */
    PetMotionAxis axis[3];
    uint32_t last_at, quiet_at;
    uint16_t linear_peak_mg;
    uint8_t initialized, quiet, stable, armed, candidate, emitted;
} PetMotion;

void pet_motion_init(PetMotion *motion);
/* 20 ms nominal samples; gaps >120 ms and invalid reads require fresh rest.
 * Units: mg and mdps. No LVGL, RTOS or pet numerical rules live here. */
uint8_t pet_motion_sample(PetMotion *motion, const int32_t accel[3],
                           const int32_t gyro[3], uint8_t valid, uint32_t now);
#endif
