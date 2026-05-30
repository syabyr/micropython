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
#include "py/mperrno.h"
#include "py/mphal.h"
#include "extmod/vfs.h"
#include "drivers/bus/spi.h"
#include "machine_pin.h"
#include "machine_spi.h"
#include "machine_spiflash.h"

#if MICROPY_PY_MACHINE_SPIFLASH

// Flash commands and geometry.
#define CMD_WREN         0x06
#define CMD_RDSR         0x05
#define CMD_READ         0x03
#define CMD_PP           0x02
#define CMD_SE           0x20
#define CMD_RDID         0x9F
#define CMD_CHIP_ERASE   0xC7

#ifndef MICROPY_HW_SPIFLASH_SIZE_BYTES
#define MICROPY_HW_SPIFLASH_SIZE_BYTES (256 * 1024)
#endif

#define SPIFLASH_ERASE_BLOCK_SIZE (4096)
#define SPIFLASH_PAGE_SIZE (256)
#define SPIFLASH_BLOCK_COUNT (MICROPY_HW_SPIFLASH_SIZE_BYTES / SPIFLASH_ERASE_BLOCK_SIZE)

#define CS_LOW(pin)  mp_hal_pin_write(pin, 0)
#define CS_HIGH(pin) mp_hal_pin_write(pin, 1)

static int machine_spiflash_hw_spi_ioctl(void *self_in, uint32_t cmd) {
    (void)self_in;
    (void)cmd;
    return 0;
}

static void machine_spiflash_hw_spi_transfer(void *self_in, size_t len, const uint8_t *src, uint8_t *dest) {
    mp_hal_spi_transfer((mp_hal_spi_obj_t)self_in, len, src, dest);
}

static const mp_spi_proto_t machine_spiflash_hw_spi_proto = {
    .ioctl = machine_spiflash_hw_spi_ioctl,
    .transfer = machine_spiflash_hw_spi_transfer,
};

static void wait_ready(machine_spiflash_obj_t *self);
static void write_enable(machine_spiflash_obj_t *self);

// 带CS控制的SPI传输
static void cs_transfer(machine_spiflash_obj_t *self,
                        size_t cmd_len, const uint8_t *cmd,
                        size_t data_len, uint8_t *data, bool read) {
    CS_LOW(self->cs);
    // 发送命令
    self->spi_proto->transfer(self->spi, cmd_len, cmd, NULL);
    // 发送/接收数据
    if (data_len > 0) {
        if (read) {
            for (size_t i = 0; i < data_len; ++i) {
                uint8_t tx = 0xff;
                self->spi_proto->transfer(self->spi, 1, &tx, &data[i]);
            }
        } else {
            self->spi_proto->transfer(self->spi, data_len, data, NULL);
        }
    }
    CS_HIGH(self->cs);
}

static int spiflash_read_bytes(machine_spiflash_obj_t *self, uint32_t addr, size_t len, uint8_t *dest) {
    uint8_t cmd[4] = {CMD_READ, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    cs_transfer(self, 4, cmd, len, dest, true);
    return 0;
}

static int spiflash_erase_block(machine_spiflash_obj_t *self, uint32_t addr) {
    if (addr % SPIFLASH_ERASE_BLOCK_SIZE != 0) {
        return -MP_EINVAL;
    }
    write_enable(self);
    uint8_t cmd[4] = {CMD_SE, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    cs_transfer(self, 4, cmd, 0, NULL, false);
    wait_ready(self);
    return 0;
}

static int spiflash_write_bytes(machine_spiflash_obj_t *self, uint32_t addr, const uint8_t *src, size_t len) {
    while (len) {
        size_t page_off = addr & (SPIFLASH_PAGE_SIZE - 1);
        size_t chunk = SPIFLASH_PAGE_SIZE - page_off;
        if (chunk > len) {
            chunk = len;
        }

        write_enable(self);
        uint8_t cmd[4] = {CMD_PP, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
        cs_transfer(self, 4, cmd, chunk, (uint8_t *)src, false);
        wait_ready(self);

        addr += chunk;
        src += chunk;
        len -= chunk;
    }
    return 0;
}

// 等待Flash空闲
static void wait_ready(machine_spiflash_obj_t *self) {
    uint8_t cmd = CMD_RDSR;
    uint8_t sr;
    do {
        cs_transfer(self, 1, &cmd, 1, &sr, true);
    } while (sr & 0x01);
}

// 写使能
static void write_enable(machine_spiflash_obj_t *self) {
    uint8_t cmd = CMD_WREN;
    CS_LOW(self->cs);
    self->spi_proto->transfer(self->spi, 1, &cmd, NULL);
    CS_HIGH(self->cs);
}

// Constructor supports either SPIFlash(spi, cs) or SPIFlash(cs, spi).
STATIC mp_obj_t machine_spiflash_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)n_kw;
    if (n_args < 2) {
        mp_raise_TypeError("SPIFlash(spi, cs)");
    }

    machine_spi_obj_t *spi_obj = NULL;
    mp_obj_t cs_obj = MP_OBJ_NULL;

    if (mp_obj_is_type(args[0], &machine_spi_type)) {
        spi_obj = MP_OBJ_TO_PTR(args[0]);
        cs_obj = args[1];
    } else if (mp_obj_is_type(args[1], &machine_spi_type)) {
        cs_obj = args[0];
        spi_obj = MP_OBJ_TO_PTR(args[1]);
    } else {
        mp_raise_TypeError("SPI object required");
    }

    mp_hal_pin_obj_t cs = MP_OBJ_NULL;
    if (mp_obj_is_type(cs_obj, &machine_pin_type)) {
        cs = (mp_hal_pin_obj_t)cs_obj;
    } else {
        cs = mp_hal_pin_lookup(mp_obj_get_int(cs_obj));
    }
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
    self->spi_proto = &machine_spiflash_hw_spi_proto;
    self->cs = cs;

    return MP_OBJ_FROM_PTR(self);
}

// Read JEDEC ID.
STATIC mp_obj_t machine_spiflash_readid(mp_obj_t self_in) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t cmd = CMD_RDID;
    uint8_t buf[3];
    cs_transfer(self, 1, &cmd, 3, buf, true);
    return mp_obj_new_bytes(buf, 3);
}
MP_DEFINE_CONST_FUN_OBJ_1(machine_spiflash_readid_obj, machine_spiflash_readid);

// Erase 4KB sector, address must be aligned.
STATIC mp_obj_t machine_spiflash_erase(mp_obj_t self_in, mp_obj_t addr_obj) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t addr = mp_obj_get_int(addr_obj);

    int ret = spiflash_erase_block(self, addr);
    if (ret != 0) {
        mp_raise_OSError(-ret);
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(machine_spiflash_erase_obj, machine_spiflash_erase);

// Read data: read(addr, buf) or read(addr, len)->bytes.
STATIC mp_obj_t machine_spiflash_read(size_t n_args, const mp_obj_t *args) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint32_t addr = mp_obj_get_int(args[1]);

    vstr_t vstr;
    mp_buffer_info_t bufinfo;

    if (mp_get_buffer(args[2], &bufinfo, MP_BUFFER_WRITE)) {
        // read(addr, buf)
        mp_int_t len = bufinfo.len;
        spiflash_read_bytes(self, addr, len, bufinfo.buf);
        return mp_const_none;
    } else {
        // read(addr, len)
        mp_int_t len = mp_obj_get_int(args[2]);
        vstr_init_len(&vstr, len);
        spiflash_read_bytes(self, addr, len, (uint8_t *)vstr.buf);
        return mp_obj_new_bytes_from_vstr(&vstr);
    }
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_spiflash_read_obj, 3, 3, machine_spiflash_read);

// Write data: write(addr, buf)
STATIC mp_obj_t machine_spiflash_write(mp_obj_t self_in, mp_obj_t addr_obj, mp_obj_t buf_obj) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t addr = mp_obj_get_int(addr_obj);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_obj, &bufinfo, MP_BUFFER_READ);
    uint32_t len = bufinfo.len;

    int ret = spiflash_write_bytes(self, addr, bufinfo.buf, len);
    if (ret != 0) {
        mp_raise_OSError(-ret);
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(machine_spiflash_write_obj, machine_spiflash_write);

STATIC mp_obj_t machine_spiflash_readblocks(size_t n_args, const mp_obj_t *args) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint32_t block = mp_obj_get_int(args[1]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_WRITE);

    uint32_t addr = block * SPIFLASH_ERASE_BLOCK_SIZE;
    if (n_args == 4) {
        addr += mp_obj_get_int(args[3]);
    }

    int ret = spiflash_read_bytes(self, addr, bufinfo.len, bufinfo.buf);
    return MP_OBJ_NEW_SMALL_INT(ret);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_spiflash_readblocks_obj, 3, 4, machine_spiflash_readblocks);

STATIC mp_obj_t machine_spiflash_writeblocks(size_t n_args, const mp_obj_t *args) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    uint32_t block = mp_obj_get_int(args[1]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_READ);

    uint32_t addr = block * SPIFLASH_ERASE_BLOCK_SIZE;
    int ret;
    if (n_args == 4) {
        addr += mp_obj_get_int(args[3]);
    } else {
        ret = spiflash_erase_block(self, addr);
        if (ret != 0) {
            return MP_OBJ_NEW_SMALL_INT(ret);
        }
    }

    ret = spiflash_write_bytes(self, addr, bufinfo.buf, bufinfo.len);
    return MP_OBJ_NEW_SMALL_INT(ret);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_spiflash_writeblocks_obj, 3, 4, machine_spiflash_writeblocks);

STATIC mp_obj_t machine_spiflash_ioctl(mp_obj_t self_in, mp_obj_t op_obj, mp_obj_t arg_obj) {
    machine_spiflash_obj_t *self = MP_OBJ_TO_PTR(self_in);
    (void)self;
    mp_int_t op = mp_obj_get_int(op_obj);
    switch (op) {
        case MP_BLOCKDEV_IOCTL_INIT:
        case MP_BLOCKDEV_IOCTL_DEINIT:
        case MP_BLOCKDEV_IOCTL_SYNC:
            return MP_OBJ_NEW_SMALL_INT(0);
        case MP_BLOCKDEV_IOCTL_BLOCK_COUNT:
            return MP_OBJ_NEW_SMALL_INT(SPIFLASH_BLOCK_COUNT);
        case MP_BLOCKDEV_IOCTL_BLOCK_SIZE:
            return MP_OBJ_NEW_SMALL_INT(SPIFLASH_ERASE_BLOCK_SIZE);
        case MP_BLOCKDEV_IOCTL_BLOCK_ERASE: {
            mp_int_t block = mp_obj_get_int(arg_obj);
            int ret = spiflash_erase_block(self, block * SPIFLASH_ERASE_BLOCK_SIZE);
            return MP_OBJ_NEW_SMALL_INT(ret);
        }
        default:
            return mp_const_none;
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(machine_spiflash_ioctl_obj, machine_spiflash_ioctl);

STATIC const mp_rom_map_elem_t machine_spiflash_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_readid),  MP_ROM_PTR(&machine_spiflash_readid_obj) },
    { MP_ROM_QSTR(MP_QSTR_read),    MP_ROM_PTR(&machine_spiflash_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write),   MP_ROM_PTR(&machine_spiflash_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_erase),   MP_ROM_PTR(&machine_spiflash_erase_obj) },
    { MP_ROM_QSTR(MP_QSTR_readblocks), MP_ROM_PTR(&machine_spiflash_readblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeblocks), MP_ROM_PTR(&machine_spiflash_writeblocks_obj) },
    { MP_ROM_QSTR(MP_QSTR_ioctl), MP_ROM_PTR(&machine_spiflash_ioctl_obj) },
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
