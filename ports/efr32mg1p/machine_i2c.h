#ifndef MICROPY_INCLUDED_EXTMOD_MACHINE_I2C_H
#define MICROPY_INCLUDED_EXTMOD_MACHINE_I2C_H
#include "py/obj.h"
// HAL层I2C对象类型
typedef struct _mp_hal_i2c_t *mp_hal_i2c_obj_t;
// HAL层函数声明
mp_hal_i2c_obj_t mp_hal_i2c_get(size_t id);
void mp_hal_i2c_get_default_pins(size_t id, uint8_t *scl, uint8_t *sda);
void mp_hal_i2c_init(mp_hal_i2c_obj_t i2c, uint32_t frequency, uint8_t scl, uint8_t sda);
void mp_hal_i2c_deinit(mp_hal_i2c_obj_t i2c);
int mp_hal_i2c_write(mp_hal_i2c_obj_t i2c, uint16_t addr, const uint8_t *data, size_t len, bool stop);
int mp_hal_i2c_read(mp_hal_i2c_obj_t i2c, uint16_t addr, uint8_t *data, size_t len, bool stop);
// I2C对象类型声明
typedef struct _machine_i2c_obj_t {
    mp_obj_base_t base;
    mp_hal_i2c_obj_t i2c;
    uint8_t id;
} machine_i2c_obj_t;

extern const mp_obj_type_t machine_i2c_type;
#endif // MICROPY_INCLUDED_EXTMOD_MACHINE_I2C_H
