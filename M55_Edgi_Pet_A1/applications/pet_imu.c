#include "pet_imu.h"
#include "pet_motion.h"
#include "pet_app.h"
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>
#include "imu_vendor/lsm6ds3tr-c_reg.h"

static struct rt_i2c_bus_device *s_bus;
static uint8_t s_address;
static stmdev_ctx_t s_device;
static struct rt_thread s_worker;
rt_align(RT_ALIGN_SIZE) static uint8_t s_stack[4096];
static PetImuSample s_sample;
static uint8_t s_started;

static int32_t read_regs(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    struct rt_i2c_msg msgs[2];
    (void)handle;
    if (!s_bus || !data || !len) return -RT_ERROR;
    msgs[0].addr = s_address; msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = &reg; msgs[0].len = 1;
    msgs[1].addr = s_address; msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = data; msgs[1].len = len;
    return rt_i2c_transfer(s_bus, msgs, 2) == 2 ? 0 : -RT_ERROR;
}

static int32_t write_regs(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    struct rt_i2c_msg msg;
    uint8_t bytes[17];
    (void)handle;
    if (!s_bus || !data || !len || len > 16) return -RT_ERROR;
    bytes[0] = reg; memcpy(bytes + 1, data, len);
    msg.addr = s_address; msg.flags = RT_I2C_WR;
    msg.buf = bytes; msg.len = len + 1;
    return rt_i2c_transfer(s_bus, &msg, 1) == 1 ? 0 : -RT_ERROR;
}

void pet_imu_get(PetImuSample *out)
{
    rt_base_t level;
    if (!out) return;
    level = rt_hw_interrupt_disable();
    *out = s_sample;
    rt_hw_interrupt_enable(level);
}

static void publish(const PetImuSample *value)
{
    rt_base_t level = rt_hw_interrupt_disable();
    s_sample = *value;
    rt_hw_interrupt_enable(level);
}

static int configure(PetImuSample *sample)
{
    uint8_t id = 0, reset = 1;
    unsigned n;
    s_bus = (struct rt_i2c_bus_device *)rt_device_find("i2c0");
    if (!s_bus) return -RT_ERROR;
    memset(&s_device, 0, sizeof(s_device));
    s_device.read_reg = read_regs; s_device.write_reg = write_regs;
    /* Probe only the two documented IMU addresses; identity is checked before writes. */
    for (n = 0; n < 2; n++) {
        s_address = (uint8_t)(0x6a + n);
        if (lsm6ds3tr_c_device_id_get(&s_device, &id) == 0 && id == LSM6DS3TR_C_ID) break;
    }
    sample->id = id;
    if (n == 2) return -RT_ERROR;
    sample->address = s_address;
    if (lsm6ds3tr_c_reset_set(&s_device, 1)) return -RT_ERROR;
    for (n = 0; n < 20; n++) {
        rt_thread_mdelay(5);
        if (lsm6ds3tr_c_reset_get(&s_device, &reset)) return -RT_ERROR;
        if (!reset) break;
    }
    if (reset) return -RT_ETIMEOUT;
    if (lsm6ds3tr_c_block_data_update_set(&s_device, 1) ||
        lsm6ds3tr_c_auto_increment_set(&s_device, 1) ||
        lsm6ds3tr_c_xl_full_scale_set(&s_device, LSM6DS3TR_C_8g) ||
        lsm6ds3tr_c_gy_full_scale_set(&s_device, LSM6DS3TR_C_500dps) ||
        lsm6ds3tr_c_xl_data_rate_set(&s_device, LSM6DS3TR_C_XL_ODR_104Hz) ||
        lsm6ds3tr_c_gy_data_rate_set(&s_device, LSM6DS3TR_C_GY_ODR_104Hz)) return -RT_ERROR;
    /* Sensor settling is bounded and does not block LVGL. */
    rt_thread_mdelay(150);
    rt_kprintf("[A3] IMU ready bus=i2c0 addr=0x%02x id=0x%02x odr=104Hz accel=8g gyro=500dps\n", s_address, id);
    return RT_EOK;
}

static void sample_loop(void *argument)
{
    PetImuSample sample = {0};
    PetMotion motion;
    lsm6ds3tr_c_status_reg_t status;
    int16_t accel[3], gyro[3];
    uint32_t last_fresh = 0;
    unsigned failures = 0, i;
    int configured = 0;
    uint8_t event;
    (void)argument;
    pet_motion_init(&motion);
    while (1) {
        if (!configured) {
            sample.ready = sample.stable = sample.armed = 0;
            pet_motion_init(&motion); publish(&sample);
            if (configure(&sample) != RT_EOK) {
                sample.errors++; publish(&sample);
                rt_kprintf("[A3] IMU unavailable; touch remains available; retry in 5s\n");
                rt_thread_mdelay(5000); continue;
            }
            configured = 1; failures = 0;
            sample.restarts++;
            last_fresh = rt_tick_get_millisecond();
        }
        memset(&status, 0, sizeof(status));
        if (lsm6ds3tr_c_status_reg_get(&s_device, &status) ||
            ((status.xlda && status.gda) &&
             (lsm6ds3tr_c_acceleration_raw_get(&s_device, accel) ||
              lsm6ds3tr_c_angular_rate_raw_get(&s_device, gyro)))) {
            sample.errors++; sample.ready = 0; failures++;
        } else if (status.xlda && status.gda) {
            failures = 0;
            for (i = 0; i < 3; i++) {
                /* ST sensitivities: 8g = 0.244 mg/LSB; 500dps = 17.5 mdps/LSB. */
                sample.accel_mg[i] = (int32_t)accel[i] * 244 / 1000;
                sample.gyro_mdps[i] = (int32_t)gyro[i] * 175 / 10;
            }
            sample.at = last_fresh = rt_tick_get_millisecond();
            sample.sequence++; sample.ready = 1;
            event = pet_motion_sample(&motion, sample.accel_mg, sample.gyro_mdps, 1, sample.at);
            sample.linear_mg = motion.linear_peak_mg;
            sample.stable = motion.stable; sample.armed = motion.armed;
            if (event) {
                if (event == PET_MOTION_HEAVY) sample.heavy_events++;
                else sample.light_events++;
                if (pet_app_post_motion(event, sample.at, RT_TRUE) != RT_EOK) sample.dropped++;
            }
        }
        if ((uint32_t)(rt_tick_get_millisecond() - last_fresh) > 150U) sample.ready = 0;
        if (!sample.ready) {
            pet_motion_init(&motion);
            sample.stable = sample.armed = 0;
        }
        if (failures >= 5 || (uint32_t)(rt_tick_get_millisecond() - last_fresh) >= 1000U) configured = 0;
        publish(&sample);
        rt_thread_mdelay(20);
    }
}

int pet_imu_init(void)
{
    int result;
    if (s_started) return -RT_EBUSY;
    result = rt_thread_init(&s_worker, "pet_imu", sample_loop, RT_NULL,
                            s_stack, sizeof(s_stack), 18, 5);
    if (result != RT_EOK) return result;
    s_started = 1;
    return rt_thread_startup(&s_worker);
}

static void print_sample(const PetImuSample *s)
{
    rt_kprintf("A3_IMU ready=%u id=%u addr=%u seq=%lu at=%lu errors=%lu restarts=%lu a=%ld,%ld,%ld g=%ld,%ld,%ld linear=%u stable=%u armed=%u light=%lu heavy=%lu dropped=%lu\n",
               s->ready, s->id, s->address, (unsigned long)s->sequence, (unsigned long)s->at,
               (unsigned long)s->errors, (unsigned long)s->restarts,
               (long)s->accel_mg[0], (long)s->accel_mg[1], (long)s->accel_mg[2],
               (long)s->gyro_mdps[0], (long)s->gyro_mdps[1], (long)s->gyro_mdps[2],
               s->linear_mg, s->stable, s->armed, (unsigned long)s->light_events,
               (unsigned long)s->heavy_events, (unsigned long)s->dropped);
}

static int cmd_imu(int argc, char **argv)
{
    PetImuSample sample;
    long seconds;
    uint32_t start, sequence = 0;
    char *end;
    if (argc == 1) { pet_imu_get(&sample); print_sample(&sample); return 0; }
    if (argc != 3 || strcmp(argv[1], "trace")) return -RT_EINVAL;
    seconds = strtol(argv[2], &end, 10);
    if (!*argv[2] || *end || seconds < 1 || seconds > 30) return -RT_EINVAL;
    start = rt_tick_get_millisecond();
    while ((uint32_t)(rt_tick_get_millisecond() - start) < (uint32_t)seconds * 1000UL) {
        pet_imu_get(&sample);
        if (sample.sequence != sequence) { print_sample(&sample); sequence = sample.sequence; }
        rt_thread_mdelay(20);
    }
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_imu, pet_imu, Read IMU status or trace N seconds without changing pet state);
