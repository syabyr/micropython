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

#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"

#include "machine_pin.h"
#include "machine_i2c.h"

#if MICROPY_PY_MACHINE_I2C

STATIC mp_obj_t machine_i2c_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args);

STATIC void machine_i2c_print(const mp_print_t *print, mp_obj_t o, mp_print_kind_t kind) {
    (void)kind;
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(o);
    mp_printf(print, "I2C(%u)", self->id);
}

STATIC mp_obj_t machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    if (n_args < 1) {
        mp_raise_TypeError("I2C id required");
    }

    mp_int_t id = mp_obj_get_int(args[0]);
    mp_hal_i2c_obj_t i2c = mp_hal_i2c_get(id);

    machine_i2c_obj_t *self = mp_obj_malloc(machine_i2c_obj_t, type);
    self->base.type = type;
    self->i2c = i2c;
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
        machine_i2c_init(n_args, init_args, &kw_args);
    }

    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t machine_i2c_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(args[0]);

    uint8_t scl, sda;
    mp_hal_i2c_get_default_pins(self->id, &scl, &sda);

    enum { ARG_freq, ARG_scl, ARG_sda };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = 100000} },
        { MP_QSTR_scl, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_sda, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };
    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, vals);

    uint32_t frequency = vals[ARG_freq].u_int;

    // scl/sda accept an int pin id or a Pin object.
    if (vals[ARG_scl].u_obj != MP_OBJ_NULL) {
        if (mp_obj_is_type(vals[ARG_scl].u_obj, &machine_pin_type)) {
            scl = mp_hal_pin_id(MP_OBJ_TO_PTR(vals[ARG_scl].u_obj));
        } else {
            scl = mp_obj_get_int(vals[ARG_scl].u_obj);
        }
    }
    if (vals[ARG_sda].u_obj != MP_OBJ_NULL) {
        if (mp_obj_is_type(vals[ARG_sda].u_obj, &machine_pin_type)) {
            sda = mp_hal_pin_id(MP_OBJ_TO_PTR(vals[ARG_sda].u_obj));
        } else {
            sda = mp_obj_get_int(vals[ARG_sda].u_obj);
        }
    }

    mp_hal_i2c_init(self->i2c, frequency, scl, sda);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_i2c_init_obj, 1, machine_i2c_init);

STATIC mp_obj_t machine_i2c_deinit(mp_obj_t self_in) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_hal_i2c_deinit(self->i2c);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_deinit_obj, machine_i2c_deinit);

STATIC mp_obj_t machine_i2c_scan(mp_obj_t self_in) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (uint16_t addr = 0; addr < 128; ++addr) {
        uint8_t buf;
        int ret = mp_hal_i2c_read(self->i2c, addr, &buf, 1, true);
        if (ret == 0) {
            mp_obj_list_append(list, MP_OBJ_NEW_SMALL_INT(addr));
        }
    }
    return list;
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_i2c_scan_obj, machine_i2c_scan);

STATIC mp_obj_t machine_i2c_writeto(size_t n_args, const mp_obj_t *args) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t addr = mp_obj_get_int(args[1]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_READ);
    bool stop = true;
    if (n_args == 4) {
        stop = mp_obj_is_true(args[3]);
    }
    int ret = mp_hal_i2c_write(self->i2c, addr, bufinfo.buf, bufinfo.len, stop);
    if (ret != 0) {
        mp_raise_OSError(-ret);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_i2c_writeto_obj, 3, 4, machine_i2c_writeto);

STATIC mp_obj_t machine_i2c_readfrom_into(size_t n_args, const mp_obj_t *args) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t addr = mp_obj_get_int(args[1]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_WRITE);
    bool stop = true;
    if (n_args == 4) {
        stop = mp_obj_is_true(args[3]);
    }
    int ret = mp_hal_i2c_read(self->i2c, addr, bufinfo.buf, bufinfo.len, stop);
    if (ret != 0) {
        mp_raise_OSError(-ret);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_i2c_readfrom_into_obj, 3, 4, machine_i2c_readfrom_into);

STATIC mp_obj_t machine_i2c_readfrom(size_t n_args, const mp_obj_t *args) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint16_t addr = mp_obj_get_int(args[1]);
    mp_int_t n = mp_obj_get_int(args[2]);
    bool stop = true;
    if (n_args == 4) {
        stop = mp_obj_is_true(args[3]);
    }

    vstr_t vstr;
    vstr_init_len(&vstr, n);
    int ret = mp_hal_i2c_read(self->i2c, addr, (uint8_t *)vstr.buf, n, stop);
    if (ret != 0) {
        vstr_clear(&vstr);
        mp_raise_OSError(-ret);
    }
    return mp_obj_new_bytes_from_vstr(&vstr);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_i2c_readfrom_obj, 3, 4, machine_i2c_readfrom);

// Convert a memory address into addrsize/8 big-endian bytes (addrsize is 8/16/32).
STATIC size_t machine_i2c_fill_memaddr(uint8_t *memaddr_buf, uint32_t memaddr, uint8_t addrsize) {
    if ((addrsize & 7) != 0 || addrsize > 32) {
        mp_raise_ValueError("invalid addrsize");
    }
    size_t len = 0;
    for (int16_t i = addrsize - 8; i >= 0; i -= 8) {
        memaddr_buf[len++] = memaddr >> i;
    }
    return len;
}

STATIC const mp_arg_t machine_i2c_mem_allowed_args[] = {
    { MP_QSTR_addr, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_memaddr, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_arg, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_addrsize, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} },
};

STATIC mp_obj_t machine_i2c_readfrom_mem(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_addr, ARG_memaddr, ARG_n, ARG_addrsize };
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_i2c_mem_allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(machine_i2c_mem_allowed_args), machine_i2c_mem_allowed_args, args);

    uint8_t memaddr_buf[4];
    size_t memaddr_len = machine_i2c_fill_memaddr(memaddr_buf, args[ARG_memaddr].u_int, args[ARG_addrsize].u_int);

    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_int_t n = mp_obj_get_int(args[ARG_n].u_obj);

    vstr_t vstr;
    vstr_init_len(&vstr, n);
    int ret = mp_hal_i2c_write_read(self->i2c, args[ARG_addr].u_int, memaddr_buf, memaddr_len, (uint8_t *)vstr.buf, n);
    if (ret != 0) {
        vstr_clear(&vstr);
        mp_raise_OSError(-ret);
    }
    return mp_obj_new_bytes_from_vstr(&vstr);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_i2c_readfrom_mem_obj, 1, machine_i2c_readfrom_mem);

STATIC mp_obj_t machine_i2c_writeto_mem(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_addr, ARG_memaddr, ARG_buf, ARG_addrsize };
    mp_arg_val_t args[MP_ARRAY_SIZE(machine_i2c_mem_allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(machine_i2c_mem_allowed_args), machine_i2c_mem_allowed_args, args);

    uint8_t memaddr_buf[4];
    size_t memaddr_len = machine_i2c_fill_memaddr(memaddr_buf, args[ARG_memaddr].u_int, args[ARG_addrsize].u_int);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_buf].u_obj, &bufinfo, MP_BUFFER_READ);

    // Concatenate memaddr + data into one write (old emlib API can't do a
    // "no-stop" write, so a single I2C_FLAG_WRITE of the combined buffer is
    // the only way to send both in one transaction).
    size_t total = memaddr_len + bufinfo.len;
    uint8_t *combined = m_new(uint8_t, total);
    memcpy(combined, memaddr_buf, memaddr_len);
    memcpy(combined + memaddr_len, bufinfo.buf, bufinfo.len);

    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    int ret = mp_hal_i2c_write(self->i2c, args[ARG_addr].u_int, combined, total, true);
    m_del(uint8_t, combined, total);
    if (ret != 0) {
        mp_raise_OSError(-ret);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_i2c_writeto_mem_obj, 1, machine_i2c_writeto_mem);

STATIC const mp_rom_map_elem_t machine_i2c_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_i2c_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_i2c_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_scan), MP_ROM_PTR(&machine_i2c_scan_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeto), MP_ROM_PTR(&machine_i2c_writeto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readfrom), MP_ROM_PTR(&machine_i2c_readfrom_obj) },
    { MP_ROM_QSTR(MP_QSTR_readfrom_into), MP_ROM_PTR(&machine_i2c_readfrom_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_readfrom_mem), MP_ROM_PTR(&machine_i2c_readfrom_mem_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeto_mem), MP_ROM_PTR(&machine_i2c_writeto_mem_obj) },
};
STATIC MP_DEFINE_CONST_DICT(machine_i2c_locals_dict, machine_i2c_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    print, machine_i2c_print,
    make_new, machine_i2c_make_new,
    locals_dict, &machine_i2c_locals_dict
    );

#endif // MICROPY_PY_MACHINE_I2C
