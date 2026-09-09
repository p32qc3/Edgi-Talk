#ifndef PET_IMU_H
#define PET_IMU_H
#include <rtthread.h>
#include <stdint.h>

typedef struct {
    int32_t accel_mg[3], gyro_mdps[3];
    uint32_t sequence, at, errors, restarts;
    uint32_t light_events, heavy_events, dropped;
    uint16_t linear_mg;
    uint8_t ready, address, id, stable, armed;
} PetImuSample;

/* One M55 worker owns the sensor; readers get an IRQ-protected copy only. */
int pet_imu_init(void);
void pet_imu_get(PetImuSample *out);
#endif
