#include <stdio.h>
#include <string.h>

#include "modmachine.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/stream.h"
#include "extmod/modmachine.h"
#include "machine_pin.h"
#include "machine_spi.h"
#include "machine_spiflash.h"
#include "machine_i2c.h"

static mp_obj_t machine_reset(void) {
    NVIC_SystemReset();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_reset_obj, machine_reset);

static mp_obj_t machine_stdio_poll(void) {
    if (mp_hal_stdio_poll(MP_STREAM_POLL_RD)) {
        return mp_const_true;
    } else {
        return mp_const_false;
    }
}
MP_DEFINE_CONST_FUN_OBJ_0(machine_stdio_poll_obj, machine_stdio_poll);

static const mp_rom_map_elem_t machine_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_umachine) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&machine_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem8), MP_ROM_PTR(&machine_mem8_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem16), MP_ROM_PTR(&machine_mem16_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem32), MP_ROM_PTR(&machine_mem32_obj) },
    { MP_ROM_QSTR(MP_QSTR_Pin), MP_ROM_PTR(&machine_pin_type) },
    { MP_ROM_QSTR(MP_QSTR_SPI), MP_ROM_PTR(&machine_spi_type) },
    { MP_ROM_QSTR(MP_QSTR_I2C), MP_ROM_PTR(&machine_i2c_type) },
    { MP_ROM_QSTR(MP_QSTR_SPIFlash), MP_ROM_PTR(&machine_spiflash_type) },
    { MP_ROM_QSTR(MP_QSTR_stdio_poll), MP_ROM_PTR(&machine_stdio_poll_obj) },
};

static MP_DEFINE_CONST_DICT(machine_module_globals, machine_module_globals_table);

const mp_obj_module_t machine_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&machine_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_machine, machine_module);
MP_REGISTER_MODULE(MP_QSTR_umachine, machine_module);
