/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
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
#include "extmod/modmachine.h"
#include "drivers/memory/spiflash.h"
#include "py/runtime.h"

typedef struct _mp_machine_flash_obj_t {
    mp_obj_base_t base;
    mp_machine_soft_spi_obj_t spi;
    mp_hal_pin_obj_t cs;
    mp_spiflash_config_t spi_flash_config;
    //mp_spiflash_cache_t spi_flash_cache;
    mp_spiflash_t spi_flash;
} mp_machine_spiflash_obj_t;

extern const mp_obj_type_t mp_machine_spiflash_type;
//extern const mp_obj_type_t mp_machine_spiflash_type;

static mp_obj_t mp_machine_spiflash_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    // 检查参数数量
    //mp_arg_check_num(n_args, n_kw, 2, 2, MP_ARGS_KW_ONLY);

/*
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_cs, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_spi, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_obj_t cs_obj = args[0].u_obj;
    mp_obj_t spi_obj = args[1].u_obj;
*/
    mp_obj_t cs_obj = all_args[0];
    mp_obj_t spi_obj = all_args[1];

    if (cs_obj == MP_OBJ_NULL || spi_obj == MP_OBJ_NULL)
	mp_raise_ValueError("cs and spi must be specified");

    // create new object
    mp_machine_spiflash_obj_t *self = m_new_obj(mp_machine_spiflash_obj_t);
    self->base.type = &mp_machine_spiflash_type;

    // set parameters
    self->spi_flash_config.bus_kind = MP_SPIFLASH_BUS_SPI;
    self->spi_flash_config.bus.u_spi.cs = cs_obj;
    //self->spi_flash_config.bus.u_spi.data = (void*) 0x20000734; //MP_OBJ_TO_PTR(spi_obj);
    self->spi_flash_config.bus.u_spi.data = (void*)(((uintptr_t) spi_obj) + 4);
    self->spi_flash_config.bus.u_spi.proto = &mp_soft_spi_proto;
    //self->spi_flash_config.cache = NULL; // for now
    self->spi_flash.config = &self->spi_flash_config;
   
    // initialize it
    mp_spiflash_init(&self->spi_flash);

    return MP_OBJ_FROM_PTR(self);
    //return MP_OBJ_FROM_PTR(&efr32_flash_obj);
}

static mp_obj_t mp_machine_spiflash_read(mp_obj_t self_obj, mp_obj_t addr_obj, mp_obj_t buf_obj)
{
    mp_machine_spiflash_obj_t * self = MP_OBJ_TO_PTR(self_obj);
    const unsigned addr = mp_obj_get_int(addr_obj);

    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_READ);
    const unsigned len = buf.len;

    mp_spiflash_read(&self->spi_flash, addr, len, buf.buf);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_spiflash_read_obj, mp_machine_spiflash_read);


static mp_obj_t mp_machine_spiflash_write(mp_obj_t self_obj, mp_obj_t addr_obj, mp_obj_t buf_obj)
{
    mp_machine_spiflash_obj_t * self = MP_OBJ_TO_PTR(self_obj);
    const unsigned addr = mp_obj_get_int(addr_obj);

    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_READ);
    const unsigned len = buf.len;

    mp_spiflash_write(&self->spi_flash, addr, len, buf.buf);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_spiflash_write_obj, mp_machine_spiflash_write);

static mp_obj_t mp_machine_spiflash_erase(mp_obj_t self_obj, mp_obj_t addr_obj)
{
    mp_machine_spiflash_obj_t * self = MP_OBJ_TO_PTR(self_obj);
    const unsigned addr = mp_obj_get_int(addr_obj);

    int ret = mp_spiflash_erase_block(&self->spi_flash, addr);
    if (ret != 0)
	mp_raise_msg(&mp_type_RuntimeError, "erase block failed");

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_spiflash_erase_obj, mp_machine_spiflash_erase);

#if 0
STATIC mp_obj_t mp_machine_spi_readinto(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_WRITE);
    memset(bufinfo.buf, n_args == 3 ? mp_obj_get_int(args[2]) : 0, bufinfo.len);
    mp_machine_spi_transfer(args[0], bufinfo.len, bufinfo.buf, bufinfo.buf);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_machine_spi_readinto_obj, 2, 3, mp_machine_spi_readinto);

STATIC mp_obj_t mp_machine_spi_write(mp_obj_t self, mp_obj_t wr_buf) {
    mp_buffer_info_t src;
    mp_get_buffer_raise(wr_buf, &src, MP_BUFFER_READ);
    mp_machine_spi_transfer(self, src.len, (const uint8_t*)src.buf, NULL);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_machine_spi_write_obj, mp_machine_spi_write);

STATIC mp_obj_t mp_machine_spi_write_readinto(mp_obj_t self, mp_obj_t wr_buf, mp_obj_t rd_buf) {
    mp_buffer_info_t src;
    mp_get_buffer_raise(wr_buf, &src, MP_BUFFER_READ);
    mp_buffer_info_t dest;
    mp_get_buffer_raise(rd_buf, &dest, MP_BUFFER_WRITE);
    if (src.len != dest.len) {
        mp_raise_ValueError("buffers must be the same length");
    }
    mp_machine_spi_transfer(self, src.len, src.buf, dest.buf);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(mp_machine_spi_write_readinto_obj, mp_machine_spi_write_readinto);
#endif

static const mp_rom_map_elem_t mp_machine_spiflash_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_machine_spiflash_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_machine_spiflash_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_erase), MP_ROM_PTR(&mp_machine_spiflash_erase_obj) },
};
MP_DEFINE_CONST_DICT(mp_machine_spiflash_locals_dict, mp_machine_spiflash_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_machine_spiflash_type,
    MP_QSTR_Flash,
    MP_TYPE_FLAG_NONE,
    make_new, mp_machine_spiflash_make_new,  // 确保有 make_new 函数
    locals_dict, &mp_machine_spiflash_locals_dict
);