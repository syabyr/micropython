/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2024
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdio.h>
#include <string.h>
#include "py/runtime.h"
#include "py/mphal.h"
#include "em_device.h"
#include "em_i2c.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "efr32mg1p_af_ports.h"
#include "efr32mg1p_af_pins.h"
#include "machine_i2c.h"
#include "mphalport.h"

#define I2C_MAX_INSTANCES 1 // 只支持1路硬件I2C (EFR32MG1P only has I2C0)

// I2C实例结构体
typedef struct _mp_hal_i2c_t {
    I2C_TypeDef *i2c;
    uint32_t frequency;
    uint8_t scl; // scl引脚编号
    uint8_t sda; // sda引脚编号
    bool initialized;
} mp_hal_i2c_t;

static mp_hal_i2c_t i2c_instances[I2C_MAX_INSTANCES] = {
    {I2C0, 100000, 0, 0, false}, // I2C0
};

static int mp_hal_i2c_find_loc_scl(I2C_TypeDef *i2c, const mp_hal_pin_obj_t scl_pin) {
    (void)i2c;
    for (int loc = 0; loc < 32; ++loc) {
        int scl_port;
        int scl_num;
        scl_port = AF_I2C0_SCL_PORT(loc);
        scl_num = AF_I2C0_SCL_PIN(loc);

        if (scl_port < 0 || scl_num < 0) {
            continue;
        }

        if (scl_port == scl_pin->port && scl_num == scl_pin->pin) {
            return loc;
        }
    }

    return -1;
}

static int mp_hal_i2c_find_loc_sda(I2C_TypeDef *i2c, const mp_hal_pin_obj_t sda_pin) {
    (void)i2c;
    for (int loc = 0; loc < 32; ++loc) {
        int sda_port;
        int sda_num;
        sda_port = AF_I2C0_SDA_PORT(loc);
        sda_num = AF_I2C0_SDA_PIN(loc);

        if (sda_port < 0 || sda_num < 0) {
            continue;
        }

        if (sda_port == sda_pin->port && sda_num == sda_pin->pin) {
            return loc;
        }
    }

    return -1;
}

// 初始化I2C
void mp_hal_i2c_init(mp_hal_i2c_obj_t i2c_obj, uint32_t frequency, uint8_t scl, uint8_t sda) {
    mp_hal_i2c_t *i2c = (mp_hal_i2c_t *)i2c_obj;
    // 保存配置
    i2c->frequency = frequency;
    i2c->scl = scl;
    i2c->sda = sda;
    // 启用I2C时钟
    if (i2c->i2c == I2C0) {
        CMU_ClockEnable(cmuClock_I2C0, true);
    }
    // 获取引脚对应的端口和引脚号
    mp_hal_pin_obj_t scl_pin = mp_hal_pin_lookup(scl);
    mp_hal_pin_obj_t sda_pin = mp_hal_pin_lookup(sda);
    if (!scl_pin || !sda_pin) {
        mp_raise_ValueError("invalid I2C pin");
    }
    // 配置SCL和SDA为开漏输出
    GPIO_PinModeSet(scl_pin->port, scl_pin->pin, gpioModeWiredAndPullUp, 1);
    GPIO_PinModeSet(sda_pin->port, sda_pin->pin, gpioModeWiredAndPullUp, 1);

    // 根据频率选择时钟占空比
    I2C_ClockHLR_TypeDef clhr;
    if (frequency <= I2C_FREQ_STANDARD_MAX) {
        clhr = i2cClockHLRStandard;
    } else if (frequency <= I2C_FREQ_FAST_MAX) {
        clhr = i2cClockHLRAsymetric;
    } else {
        clhr = i2cClockHLRFast;
    }

    // 初始化I2C为主模式
    I2C_Init_TypeDef init = I2C_INIT_DEFAULT;
    init.freq = frequency;
    init.clhr = clhr;
    I2C_Init(i2c->i2c, &init);

    // 查找引脚位置
    int scl_loc = mp_hal_i2c_find_loc_scl(i2c->i2c, scl_pin);
    int sda_loc = mp_hal_i2c_find_loc_sda(i2c->i2c, sda_pin);
    if (scl_loc < 0 || sda_loc < 0) {
        mp_raise_ValueError("no AF route for I2C pins");
    }

    // 配置路由
    i2c->i2c->ROUTELOC0 = (i2c->i2c->ROUTELOC0
        & ~(_I2C_ROUTELOC0_SDALOC_MASK | _I2C_ROUTELOC0_SCLLOC_MASK))
        | (scl_loc << _I2C_ROUTELOC0_SCLLOC_SHIFT)
        | (sda_loc << _I2C_ROUTELOC0_SDALOC_SHIFT);
    // 启用I2C引脚
    i2c->i2c->ROUTEPEN = I2C_ROUTEPEN_SCLPEN | I2C_ROUTEPEN_SDAPEN;
    i2c->initialized = true;
}

// 反初始化I2C
void mp_hal_i2c_deinit(mp_hal_i2c_obj_t i2c_obj) {
    mp_hal_i2c_t *i2c = (mp_hal_i2c_t *)i2c_obj;
    if (!i2c->initialized) return;
    // 禁用I2C
    I2C_Enable(i2c->i2c, false);
    // 关闭时钟
    if (i2c->i2c == I2C0) {
        CMU_ClockEnable(cmuClock_I2C0, false);
    }
    // 复位引脚为输入
    mp_hal_pin_obj_t scl_pin = mp_hal_pin_lookup(i2c->scl);
    mp_hal_pin_obj_t sda_pin = mp_hal_pin_lookup(i2c->sda);
    if (scl_pin) mp_hal_pin_input(scl_pin);
    if (sda_pin) mp_hal_pin_input(sda_pin);
    i2c->initialized = false;
}

// I2C写操作
int mp_hal_i2c_write(mp_hal_i2c_obj_t i2c_obj, uint16_t addr, const uint8_t *data, size_t len, bool stop) {
    mp_hal_i2c_t *i2c = (mp_hal_i2c_t *)i2c_obj;
    if (!i2c->initialized) mp_raise_ValueError("I2C not initialized");

    I2C_TransferSeq_TypeDef seq;
    seq.addr = addr << 1; // 7-bit address shifted left
    seq.flags = stop ? I2C_FLAG_WRITE : I2C_FLAG_WRITE;
    seq.buf[0].data = (uint8_t *)data;
    seq.buf[0].len = len;

    I2C_TransferReturn_TypeDef ret;
    ret = I2C_TransferInit(i2c->i2c, &seq);
    while (ret == i2cTransferInProgress) {
        ret = I2C_Transfer(i2c->i2c);
    }

    if (ret != i2cTransferDone) {
        // 错误返回负数错误码
        return (int)ret;
    }
    return 0;
}

// I2C读操作
int mp_hal_i2c_read(mp_hal_i2c_obj_t i2c_obj, uint16_t addr, uint8_t *data, size_t len, bool stop) {
    mp_hal_i2c_t *i2c = (mp_hal_i2c_t *)i2c_obj;
    if (!i2c->initialized) mp_raise_ValueError("I2C not initialized");

    I2C_TransferSeq_TypeDef seq;
    seq.addr = (addr << 1) | 1; // 7-bit address shifted left with R/W bit set
    seq.flags = stop ? I2C_FLAG_READ : I2C_FLAG_READ;
    seq.buf[0].data = data;
    seq.buf[0].len = len;

    I2C_TransferReturn_TypeDef ret;
    ret = I2C_TransferInit(i2c->i2c, &seq);
    while (ret == i2cTransferInProgress) {
        ret = I2C_Transfer(i2c->i2c);
    }

    if (ret != i2cTransferDone) {
        return (int)ret;
    }
    return 0;
}

// 获取I2C实例
mp_hal_i2c_obj_t mp_hal_i2c_get(size_t id) {
    if (id >= I2C_MAX_INSTANCES) {
        mp_raise_ValueError("invalid I2C id");
    }
    return &i2c_instances[id];
}

// 获取I2C默认引脚配置
void mp_hal_i2c_get_default_pins(size_t id, uint8_t *scl, uint8_t *sda) {
    // 默认I2C0引脚: SCL=0, SDA=1 (可根据实际板子修改)
    *scl = 0;
    *sda = 1;
}
