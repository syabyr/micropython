#ifndef MICROPY_INCLUDED_MACHINE_SPIFLASH_H
#define MICROPY_INCLUDED_MACHINE_SPIFLASH_H

#include "py/obj.h"
#include "py/mphal.h"
#include "drivers/bus/spi.h"
#include "machine_pin.h"
#include "machine_spi.h"

typedef struct _machine_spiflash_obj_t {
    mp_obj_base_t base;
    void *spi;
    const mp_spi_proto_t *spi_proto;
    mp_hal_pin_obj_t cs;
} machine_spiflash_obj_t;

extern const mp_spi_proto_t machine_spiflash_hw_spi_proto;

extern const mp_obj_type_t machine_spiflash_type;

#endif // MICROPY_INCLUDED_MACHINE_SPIFLASH_H
