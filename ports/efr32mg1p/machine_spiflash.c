/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
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
#include "machine_pin.h"
#include "machine_spi.h"
#include "machine_spiflash.h"

#if MICROPY_PY_MACHINE_SPIFLASH

// Flash命令
#define CMD_WREN         0x06
#define CMD_RDSR         0x05
#define CMD_READ         0x03
#define CMD_PP           0x02
#define CMD_SE           0x20
#define CMD_RDID         0x9F
#define CMD_CHIP_ERASE   0xC7

#define CS_LOW(pin)  mp_hal_pin_write(pin, 0)
#define CS_HIGH(pin) mp_hal_pin_write(pin, 1)

// 带CS控制的SPI传输
static void cs_transfer(mp_hal_spi_obj_t spi, mp_hal_pin_obj_t cs,
                        size_t cmd_len, const uint8_t *cmd,
                        size_t data_len, uint8_t *data, bool read) {
    CS_LOW(cs);
    // 发送命令
    for (size_t i = 0; i < cmd_len; i++) {
        uint8_t tx = cmd[i];
        mp_hal_spi_transfer(spi, 1, &tx, NULL);
    }
    // 发送/接收数据
    if (data_len > 0) {
        mp_hal_spi_transfer(spi, data_len, read ? NULL : data,
                            read ? data : NULL);
    }
    CS_HIGH(cs);
}

// 等待Flash空闲
static void wait_ready(mp_hal_spi_obj_t spi, mp_hal_pin_obj_t cs) {
    uint8_t cmd = CMD_RDSR;
    uint8_t sr;
    do {
        cs_transfer(spi, cs, 1, &cmd, 1, &sr, true);
    } while (sr & 0x01);
}

// 写使能
static void write_enable(mp_hal_spi_obj_t spi, mp_hal_pin_obj_t cs) {
    uint8_t cmd = CMD_WREN;
    CS_LOW(cs);
    mp_hal_spi_transfer(spi, 1, &cmd, NULL);
    CS_HIGH(cs);
}

// 构造函数: SPIFlash(spi, cs)
STATIC mp_obj_t machine_spiflash_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // 需要2个参数: spi对象和cs引脚
    if (n_args < 2) {
        mp_raise_TypeError("SPIFlash(spi, cs)");
    }

    // 从参数获取spi对象（第一个参数是spi实例）
    machine_spi_obj_t *spi_obj = MP_OBJ_TO_PTR(args[0]);

    // 获取cs引脚
    mp_int_t cs_pin_id = mp_obj_get_int(args[1]);
    mp_hal_pin_obj_t cs = mp_hal_pin_lookup(cs_pin_id);
    if (cs == NULL) {
        mp_raise_ValueError("invalid CS pin");
    }

    // 初始化CS引脚为输出高电平
    mp_hal_pin_output(cs);
    mp_hal_pin_write(cs, 1);

    // 创建对象
    machine_spiflash_obj_t *self = mp_obj_malloc(machine_spiflash_obj_t, type);
    self->base.type = type;
    self->spi = spi_obj->spi;
    self->cs = cs;

    return MP_OBJ_FROM_PTR(self);
}

// 读取JEDEC ID
STATIC mp_obj_t machine_spiflash_readid(mp_obj_t self_in) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t cmd[4] = {CMD_RDID, 0, 0, 0};
    uint8_t buf[3];
    cs_transfer(self->spi, self->cs, 4, cmd, 3, buf, true);
    return mp_obj_new_bytes(buf, 3);
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_spiflash_readid_obj, machine_spiflash_readid);

// 擦除扇区(4KB)，addr必须4KB对齐
STATIC mp_obj_t machine_spiflash_erase(mp_obj_t self_in, mp_obj_t addr_obj) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t addr = mp_obj_get_int(addr_obj);

    write_enable(self->spi, self->cs);
    uint8_t cmd[4] = {CMD_SE, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF};
    cs_transfer(self->spi, self->cs, 4, cmd, 0, NULL, true);
    wait_ready(self->spi, self->cs);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(machine_spiflash_erase_obj, machine_spiflash_erase);

// 读取数据: read(addr, buf) 或 read(addr, len)返回bytes
STATIC mp_obj_t machine_spiflash_read(size_t n_args, const mp_obj_t *args) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint32_t addr = mp_obj_get_int(args[1]);

    vstr_t vstr;
    mp_buffer_info_t bufinfo;

    if (mp_get_buffer(args[2], &bufinfo, MP_BUFFER_WRITE)) {
        // read(addr, buf) - 读入buffer
        mp_int_t len = bufinfo.len;
        uint8_t cmd[4] = {CMD_READ, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF};
        cs_transfer(self->spi, self->cs, 4, cmd, len, bufinfo.buf, true);
        return mp_const_none;
    } else {
        // read(addr, len) - 返回bytes
        mp_int_t len = mp_obj_get_int(args[2]);
        vstr_init_len(&vstr, len);
        uint8_t cmd[4] = {CMD_READ, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF};
        cs_transfer(self->spi, self->cs, 4, cmd, len, (uint8_t*)vstr.buf, true);
        return mp_obj_new_bytes_from_vstr(&vstr);
    }
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_spiflash_read_obj, 3, 3, machine_spiflash_read);

// 写入数据: write(addr, buf)
STATIC mp_obj_t machine_spiflash_write(mp_obj_t self_in, mp_obj_t addr_obj, mp_obj_t buf_obj) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t addr = mp_obj_get_int(addr_obj);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_obj, &bufinfo, MP_BUFFER_READ);
    uint32_t len = bufinfo.len;

    write_enable(self->spi, self->cs);
    uint8_t cmd[4] = {CMD_PP, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF};
    cs_transfer(self->spi, self->cs, 4, cmd, len, bufinfo.buf, true);
    wait_ready(self->spi, self->cs);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(machine_spiflash_write_obj, machine_spiflash_write);

STATIC const mp_rom_map_elem_t machine_spiflash_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_readid),  MP_ROM_PTR(&machine_spiflash_readid_obj) },
    { MP_ROM_QSTR(MP_QSTR_read),    MP_ROM_PTR(&machine_spiflash_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),   MP_ROM_PTR(&machine_spiflash_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_erase),   MP_ROM_PTR(&machine_spiflash_erase_obj) },
};
STATIC MP_DEFINE_CONST_DICT(machine_spiflash_locals_dict, machine_spiflash_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    machine_spiflash_type,
    MP_QSTR_SPIFlash,
    MP_TYPE_FLAG_NONE,
    make_new, machine_spiflash_make_new,
    locals_dict, &machine_spiflash_locals_dict
);

#endif // MICROPY_PY_MACHINE_SPIFLASH
