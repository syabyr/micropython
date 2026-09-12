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

/*
 * Hardware I2C (master) for the EFR32MG21 (Series 2).
 *
 * Series 2 routes I2C signals through the GPIO module's I2CROUTE[n]
 * registers (port in bits [1:0], pin in bits [20:16]) rather than the Series 1
 * I2C ROUTELOC0/ROUTEPEN registers + alternate-function lookup tables.  The
 * route index n equals the I2C number (I2C0 -> 0, I2C1 -> 1).  On the MG21 the
 * pins each I2C peripheral can drive are fixed by the DBUS matrix: I2C0's SCL
 * is PA00 and SDA is PA04.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "em_i2c.h"
#include "em_cmu.h"
#include "em_gpio.h"

#include "machine_i2c.h"
#include "mphalport.h"

#define I2C_MAX_INSTANCES 2

typedef struct _mp_hal_i2c_t {
    I2C_TypeDef *i2c;
    uint32_t frequency;
    uint8_t scl;
    uint8_t sda;
    bool initialized;
} mp_hal_i2c_t;

// Instance id equals the I2C number.  The breakout board uses I2C0
// (SCL=PA00, SDA=PA04); I2C1 is included for completeness.
static mp_hal_i2c_t i2c_instances[I2C_MAX_INSTANCES] = {
    {I2C0, 100000, 0, 0, false}, // I2C0
    {I2C1, 100000, 0, 0, false}, // I2C1
};

static uint32_t mp_hal_i2c_route_index(I2C_TypeDef *i2c) {
    return (i2c == I2C0) ? 0 : 1;
}

static CMU_Clock_TypeDef mp_hal_i2c_clock(I2C_TypeDef *i2c) {
    return (i2c == I2C0) ? cmuClock_I2C0 : cmuClock_I2C1;
}

// Run an emlib I2C sequence to completion (polling); return 0 on success or a
// negative error code (mirrors I2C_TransferReturn_TypeDef values).
static int mp_hal_i2c_transfer(I2C_TypeDef *i2c, I2C_TransferSeq_TypeDef *seq) {
    I2C_TransferReturn_TypeDef ret = I2C_TransferInit(i2c, seq);
    while (ret == i2cTransferInProgress) {
        ret = I2C_Transfer(i2c);
    }
    return (ret == i2cTransferDone) ? 0 : (int)ret;
}

void mp_hal_i2c_init(mp_hal_i2c_obj_t i2c_obj, uint32_t frequency, uint8_t scl, uint8_t sda) {
    mp_hal_i2c_t *i2c = (mp_hal_i2c_t *)i2c_obj;

    i2c->frequency = frequency;
    i2c->scl = scl;
    i2c->sda = sda;

    CMU_ClockEnable(mp_hal_i2c_clock(i2c->i2c), true);

    mp_hal_pin_obj_t scl_pin = mp_hal_pin_lookup(scl);
    mp_hal_pin_obj_t sda_pin = mp_hal_pin_lookup(sda);
    if (!scl_pin || !sda_pin) {
        mp_raise_ValueError("invalid I2C pin");
    }

    // SCL and SDA are open-drain with pull-ups.
    GPIO_PinModeSet(scl_pin->port, scl_pin->pin, gpioModeWiredAndPullUp, 1);
    GPIO_PinModeSet(sda_pin->port, sda_pin->pin, gpioModeWiredAndPullUp, 1);

    // Pick the clock high/low ratio for the requested bus frequency.
    I2C_ClockHLR_TypeDef clhr;
    if (frequency <= I2C_FREQ_STANDARD_MAX) {
        clhr = i2cClockHLRStandard;
    } else if (frequency <= I2C_FREQ_FAST_MAX) {
        clhr = i2cClockHLRAsymetric;
    } else {
        clhr = i2cClockHLRFast;
    }

    // Initialize as I2C master; refFreq == 0 means "use current clock".
    I2C_Init_TypeDef init = I2C_INIT_DEFAULT;
    init.freq = frequency;
    init.clhr = clhr;
    I2C_Init(i2c->i2c, &init);

    // Series 2 routing via the GPIO module.
    uint32_t n = mp_hal_i2c_route_index(i2c->i2c);
    GPIO->I2CROUTE[n].SCLROUTE = ((uint32_t)scl_pin->port << _GPIO_I2C_SCLROUTE_PORT_SHIFT)
                                 | ((uint32_t)scl_pin->pin << _GPIO_I2C_SCLROUTE_PIN_SHIFT);
    GPIO->I2CROUTE[n].SDAROUTE = ((uint32_t)sda_pin->port << _GPIO_I2C_SDAROUTE_PORT_SHIFT)
                                 | ((uint32_t)sda_pin->pin << _GPIO_I2C_SDAROUTE_PIN_SHIFT);
    GPIO->I2CROUTE[n].ROUTEEN = GPIO_I2C_ROUTEEN_SCLPEN | GPIO_I2C_ROUTEEN_SDAPEN;

    i2c->initialized = true;
}

void mp_hal_i2c_deinit(mp_hal_i2c_obj_t i2c_obj) {
    mp_hal_i2c_t *i2c = (mp_hal_i2c_t *)i2c_obj;
    if (!i2c->initialized) {
        return;
    }

    I2C_Enable(i2c->i2c, false);
    CMU_ClockEnable(mp_hal_i2c_clock(i2c->i2c), false);

    mp_hal_pin_obj_t scl_pin = mp_hal_pin_lookup(i2c->scl);
    mp_hal_pin_obj_t sda_pin = mp_hal_pin_lookup(i2c->sda);
    if (scl_pin) {
        mp_hal_pin_input(scl_pin);
    }
    if (sda_pin) {
        mp_hal_pin_input(sda_pin);
    }

    i2c->initialized = false;
}

int mp_hal_i2c_write(mp_hal_i2c_obj_t i2c_obj, uint16_t addr, const uint8_t *data, size_t len, bool stop) {
    (void)stop; // old emlib API always generates STOP on I2C_FLAG_WRITE
    mp_hal_i2c_t *i2c = (mp_hal_i2c_t *)i2c_obj;
    if (!i2c->initialized) {
        mp_raise_ValueError("I2C not initialized");
    }

    I2C_TransferSeq_TypeDef seq;
    seq.addr = addr << 1;
    seq.flags = I2C_FLAG_WRITE;
    seq.buf[0].data = (uint8_t *)data;
    seq.buf[0].len = len;

    return mp_hal_i2c_transfer(i2c->i2c, &seq);
}

int mp_hal_i2c_read(mp_hal_i2c_obj_t i2c_obj, uint16_t addr, uint8_t *data, size_t len, bool stop) {
    (void)stop; // old emlib API always generates STOP on I2C_FLAG_READ
    mp_hal_i2c_t *i2c = (mp_hal_i2c_t *)i2c_obj;
    if (!i2c->initialized) {
        mp_raise_ValueError("I2C not initialized");
    }

    I2C_TransferSeq_TypeDef seq;
    seq.addr = (addr << 1) | 1;
    seq.flags = I2C_FLAG_READ;
    seq.buf[0].data = data;
    seq.buf[0].len = len;

    return mp_hal_i2c_transfer(i2c->i2c, &seq);
}

int mp_hal_i2c_write_read(mp_hal_i2c_obj_t i2c_obj, uint16_t addr, const uint8_t *wdata, size_t wlen, uint8_t *rdata, size_t rlen) {
    mp_hal_i2c_t *i2c = (mp_hal_i2c_t *)i2c_obj;
    if (!i2c->initialized) {
        mp_raise_ValueError("I2C not initialized");
    }

    I2C_TransferSeq_TypeDef seq;
    seq.addr = addr << 1;
    seq.flags = I2C_FLAG_WRITE_READ;
    seq.buf[0].data = (uint8_t *)wdata;
    seq.buf[0].len = wlen;
    seq.buf[1].data = rdata;
    seq.buf[1].len = rlen;

    return mp_hal_i2c_transfer(i2c->i2c, &seq);
}

mp_hal_i2c_obj_t mp_hal_i2c_get(size_t id) {
    if (id >= I2C_MAX_INSTANCES) {
        mp_raise_ValueError("invalid I2C id");
    }
    return &i2c_instances[id];
}

void mp_hal_i2c_get_default_pins(size_t id, uint8_t *scl, uint8_t *sda) {
    (void)id;
    // EFR32MG21 breakout board I2C0: SCL=PA00 (pin 0), SDA=PA04 (pin 1).
    *scl = 0;
    *sda = 1;
}
