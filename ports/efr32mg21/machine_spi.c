/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016-2020 Damien P. George
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

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"

#include "machine_pin.h"
#include "machine_spi.h"

#if MICROPY_PY_MACHINE_SPI

STATIC mp_obj_t machine_spi_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args);

STATIC void machine_spi_print(const mp_print_t *print, mp_obj_t o, mp_print_kind_t kind) {
    (void)kind;
    machine_spi_obj_t *self = MP_OBJ_TO_PTR(o);
    mp_printf(print, "SPI(%u)", self->id);
}

STATIC mp_obj_t machine_spi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    if (n_args < 1) {
        mp_raise_TypeError("SPI id required");
    }

    mp_int_t id = mp_obj_get_int(args[0]);
    mp_hal_spi_obj_t spi = mp_hal_spi_get(id);

    machine_spi_obj_t *self = mp_obj_malloc(machine_spi_obj_t, type);
    self->base.type = type;
    self->spi = spi;
    self->id = id;

    // If init args were supplied, run init() now.
    if (n_args > 1 || n_kw > 0) {
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        mp_obj_t init_args[n_args];
        init_args[0] = MP_OBJ_FROM_PTR(self);
        for (size_t i = 1; i < n_args; ++i) {
            init_args[i] = args[i];
        }
        machine_spi_init(n_args, init_args, &kw_args);
    }

    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t machine_spi_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    machine_spi_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    uint32_t baudrate = 1000000;
    uint8_t polarity = 0;
    uint8_t phase = 0;
    uint8_t bits = 8;
    uint8_t firstbit = MICROPY_PY_MACHINE_SPI_MSB;
    uint8_t sck, mosi, miso;
    mp_hal_spi_get_default_pins(self->id, &sck, &mosi, &miso);

    enum { ARG_baudrate, ARG_polarity, ARG_phase, ARG_bits, ARG_firstbit, ARG_sck, ARG_mosi, ARG_miso };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = 1000000} },
        { MP_QSTR_polarity, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_phase, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_firstbit, MP_ARG_INT, {.u_int = MICROPY_PY_MACHINE_SPI_MSB} },
        { MP_QSTR_sck, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_mosi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_miso, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, vals);

    baudrate = vals[ARG_baudrate].u_int;
    polarity = vals[ARG_polarity].u_int;
    phase = vals[ARG_phase].u_int;
    bits = vals[ARG_bits].u_int;
    firstbit = vals[ARG_firstbit].u_int;

    // sck/mosi/miso accept an int pin id or a Pin object.
    if (vals[ARG_sck].u_obj != MP_OBJ_NULL) {
        if (mp_obj_is_type(vals[ARG_sck].u_obj, &machine_pin_type)) {
            sck = mp_hal_pin_id(MP_OBJ_TO_PTR(vals[ARG_sck].u_obj));
        } else {
            sck = mp_obj_get_int(vals[ARG_sck].u_obj);
        }
    }
    if (vals[ARG_mosi].u_obj != MP_OBJ_NULL) {
        if (mp_obj_is_type(vals[ARG_mosi].u_obj, &machine_pin_type)) {
            mosi = mp_hal_pin_id(MP_OBJ_TO_PTR(vals[ARG_mosi].u_obj));
        } else {
            mosi = mp_obj_get_int(vals[ARG_mosi].u_obj);
        }
    }
    if (vals[ARG_miso].u_obj != MP_OBJ_NULL) {
        if (mp_obj_is_type(vals[ARG_miso].u_obj, &machine_pin_type)) {
            miso = mp_hal_pin_id(MP_OBJ_TO_PTR(vals[ARG_miso].u_obj));
        } else {
            miso = mp_obj_get_int(vals[ARG_miso].u_obj);
        }
    }

    mp_hal_spi_init(self->spi, baudrate, polarity, phase, bits, firstbit, sck, mosi, miso);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_spi_init_obj, 1, machine_spi_init);

STATIC mp_obj_t machine_spi_deinit(mp_obj_t self_in) {
    machine_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_hal_spi_deinit(self->spi);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_spi_deinit_obj, machine_spi_deinit);

STATIC mp_obj_t machine_spi_read(size_t n_args, const mp_obj_t *args) {
    machine_spi_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t len = mp_obj_get_int(args[1]);
    vstr_t vstr;
    vstr_init_len(&vstr, len);
    uint8_t write_byte = 0xFF;
    if (n_args == 3) {
        write_byte = mp_obj_get_int(args[2]);
    }
    mp_hal_spi_transfer(self->spi, len, &write_byte, (uint8_t *)vstr.buf);
    return mp_obj_new_bytes_from_vstr(&vstr);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_spi_read_obj, 2, 3, machine_spi_read);

STATIC mp_obj_t machine_spi_write(mp_obj_t self_in, mp_obj_t buf) {
    machine_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_READ);
    mp_hal_spi_transfer(self->spi, bufinfo.len, bufinfo.buf, NULL);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(machine_spi_write_obj, machine_spi_write);

STATIC mp_obj_t machine_spi_write_readinto(mp_obj_t self_in, mp_obj_t write_buf, mp_obj_t read_buf) {
    machine_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t write_bufinfo;
    mp_get_buffer_raise(write_buf, &write_bufinfo, MP_BUFFER_READ);
    mp_buffer_info_t read_bufinfo;
    mp_get_buffer_raise(read_buf, &read_bufinfo, MP_BUFFER_WRITE);
    if (write_bufinfo.len != read_bufinfo.len) {
        mp_raise_ValueError("buffers must be equal length");
    }
    mp_hal_spi_transfer(self->spi, write_bufinfo.len, write_bufinfo.buf, read_bufinfo.buf);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(machine_spi_write_readinto_obj, machine_spi_write_readinto);

STATIC const mp_rom_map_elem_t machine_spi_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_spi_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_spi_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&machine_spi_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&machine_spi_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_readinto), MP_ROM_PTR(&machine_spi_write_readinto_obj) },

    { MP_ROM_QSTR(MP_QSTR_MSB), MP_ROM_INT(MICROPY_PY_MACHINE_SPI_MSB) },
    { MP_ROM_QSTR(MP_QSTR_LSB), MP_ROM_INT(MICROPY_PY_MACHINE_SPI_LSB) },
};
STATIC MP_DEFINE_CONST_DICT(machine_spi_locals_dict, machine_spi_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_spi_type,
    MP_QSTR_SPI,
    MP_TYPE_FLAG_NONE,
    print, machine_spi_print,
    make_new, machine_spi_make_new,
    locals_dict, &machine_spi_locals_dict
    );

#endif // MICROPY_PY_MACHINE_SPI
