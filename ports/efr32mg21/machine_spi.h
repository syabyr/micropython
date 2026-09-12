#ifndef MICROPY_INCLUDED_EFR32MG21_MACHINE_SPI_H
#define MICROPY_INCLUDED_EFR32MG21_MACHINE_SPI_H

#include <stddef.h>
#include "py/obj.h"

// HAL-layer SPI object type (opaque).
typedef struct _mp_hal_spi_t *mp_hal_spi_obj_t;

mp_hal_spi_obj_t mp_hal_spi_get(size_t id);
void mp_hal_spi_get_default_pins(size_t id, uint8_t *sck, uint8_t *mosi, uint8_t *miso);
void mp_hal_spi_init(mp_hal_spi_obj_t spi_obj, uint32_t baudrate, uint8_t polarity, uint8_t phase, uint8_t bits, uint8_t firstbit, uint8_t sck, uint8_t mosi, uint8_t miso);
void mp_hal_spi_deinit(mp_hal_spi_obj_t spi_obj);
void mp_hal_spi_transfer(mp_hal_spi_obj_t spi_obj, size_t len, const uint8_t *tx_buf, uint8_t *rx_buf);

typedef struct _machine_spi_obj_t {
    mp_obj_base_t base;
    mp_hal_spi_obj_t spi;
    uint8_t id;
} machine_spi_obj_t;

extern const mp_obj_type_t machine_spi_type;

#endif // MICROPY_INCLUDED_EFR32MG21_MACHINE_SPI_H
