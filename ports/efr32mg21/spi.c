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

/*
 * Hardware SPI (USART in synchronous mode) for the EFR32MG21 (Series 2).
 *
 * Series 2 routes USART signals through the GPIO module's USARTROUTE[n]
 * registers (port in bits [1:0], pin in bits [20:16]) rather than the Series 1
 * USART ROUTELOC0/ROUTEPEN registers + alternate-function lookup tables.  The
 * route index n equals the USART number (USART0 -> 0, USART1 -> 1).
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "em_usart.h"
#include "em_cmu.h"
#include "em_gpio.h"

#include "machine_spi.h"
#include "mphalport.h"

#define SPI_MAX_INSTANCES 3

typedef struct _mp_hal_spi_t {
    USART_TypeDef *usart;
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    uint8_t firstbit;
    uint8_t sck;
    uint8_t mosi;
    uint8_t miso;
    bool initialized;
} mp_hal_spi_t;

// On Series 2 the DBUS pins a USART can drive are fixed: USART0 -> PA/PB/PC/PD,
// USART1 -> PA/PB only, USART2 -> PC/PD only.  The SPI flash (PC00..PC03) is on
// USART2, so that is the default instance below.  The instance id equals the
// USART number.
static mp_hal_spi_t spi_instances[SPI_MAX_INSTANCES] = {
    {USART0, 1000000, 0, 0, 8, 0, 0, 0, 0, false}, // SPI0 -> USART0
    {USART1, 1000000, 0, 0, 8, 0, 0, 0, 0, false}, // SPI1 -> USART1
    {USART2, 1000000, 0, 0, 8, 0, 0, 0, 0, false}, // SPI2 -> USART2 (SPI flash)
};

static uint32_t mp_hal_spi_route_index(USART_TypeDef *usart) {
    if (usart == USART0) {
        return 0;
    } else if (usart == USART1) {
        return 1;
    } else {
        return 2;
    }
}

static CMU_Clock_TypeDef mp_hal_spi_clock(USART_TypeDef *usart) {
    if (usart == USART0) {
        return cmuClock_USART0;
    } else if (usart == USART1) {
        return cmuClock_USART1;
    } else {
        return cmuClock_USART2;
    }
}

void mp_hal_spi_init(mp_hal_spi_obj_t spi_obj, uint32_t baudrate, uint8_t polarity, uint8_t phase, uint8_t bits, uint8_t firstbit, uint8_t sck, uint8_t mosi, uint8_t miso) {
    mp_hal_spi_t *spi = (mp_hal_spi_t *)spi_obj;

    spi->baudrate = baudrate;
    spi->polarity = polarity;
    spi->phase = phase;
    spi->bits = bits;
    spi->firstbit = firstbit;
    spi->sck = sck;
    spi->mosi = mosi;
    spi->miso = miso;

    // Enable the USART clock.
    CMU_ClockEnable(mp_hal_spi_clock(spi->usart), true);

    mp_hal_pin_obj_t sck_pin = mp_hal_pin_lookup(sck);
    mp_hal_pin_obj_t mosi_pin = mp_hal_pin_lookup(mosi);
    mp_hal_pin_obj_t miso_pin = mp_hal_pin_lookup(miso);
    if (!sck_pin || !mosi_pin || !miso_pin) {
        mp_raise_ValueError("invalid SPI pin");
    }

    // SCK and MOSI are push-pull outputs, MISO is an input.
    GPIO_PinModeSet(sck_pin->port, sck_pin->pin, gpioModePushPull, polarity);
    GPIO_PinModeSet(mosi_pin->port, mosi_pin->pin, gpioModePushPull, 0);
    GPIO_PinModeSet(miso_pin->port, miso_pin->pin, gpioModeInput, 0);

    USART_InitSync_TypeDef init = USART_INITSYNC_DEFAULT;
    init.baudrate = baudrate;
    init.databits = usartDatabits8;
    init.master = true;         // SPI master
    init.msbf = (firstbit == 0); // MSB-first unless firstbit == 1
    init.clockMode = polarity | (phase << 1); // CPOL/CPHA -> usartClockMode0..3
    init.autoCsEnable = false;  // CS is driven manually by the block device
    USART_InitSync(spi->usart, &init);

    // Series 2 routing via the GPIO module.
    uint32_t n = mp_hal_spi_route_index(spi->usart);
    GPIO->USARTROUTE[n].TXROUTE = ((uint32_t)mosi_pin->port << _GPIO_USART_TXROUTE_PORT_SHIFT)
                                  | ((uint32_t)mosi_pin->pin << _GPIO_USART_TXROUTE_PIN_SHIFT);
    GPIO->USARTROUTE[n].RXROUTE = ((uint32_t)miso_pin->port << _GPIO_USART_RXROUTE_PORT_SHIFT)
                                  | ((uint32_t)miso_pin->pin << _GPIO_USART_RXROUTE_PIN_SHIFT);
    GPIO->USARTROUTE[n].CLKROUTE = ((uint32_t)sck_pin->port << _GPIO_USART_CLKROUTE_PORT_SHIFT)
                                   | ((uint32_t)sck_pin->pin << _GPIO_USART_CLKROUTE_PIN_SHIFT);
    GPIO->USARTROUTE[n].ROUTEEN = GPIO_USART_ROUTEEN_TXPEN
                                  | GPIO_USART_ROUTEEN_RXPEN
                                  | GPIO_USART_ROUTEEN_CLKPEN;

    spi->initialized = true;
}

void mp_hal_spi_deinit(mp_hal_spi_obj_t spi_obj) {
    mp_hal_spi_t *spi = (mp_hal_spi_t *)spi_obj;
    if (!spi->initialized) {
        return;
    }

    USART_Enable(spi->usart, usartDisable);

    CMU_ClockEnable(mp_hal_spi_clock(spi->usart), false);

    mp_hal_pin_obj_t sck_pin = mp_hal_pin_lookup(spi->sck);
    mp_hal_pin_obj_t mosi_pin = mp_hal_pin_lookup(spi->mosi);
    mp_hal_pin_obj_t miso_pin = mp_hal_pin_lookup(spi->miso);
    if (sck_pin) {
        mp_hal_pin_input(sck_pin);
    }
    if (mosi_pin) {
        mp_hal_pin_input(mosi_pin);
    }
    if (miso_pin) {
        mp_hal_pin_input(miso_pin);
    }

    spi->initialized = false;
}

void mp_hal_spi_transfer(mp_hal_spi_obj_t spi_obj, size_t len, const uint8_t *tx_buf, uint8_t *rx_buf) {
    mp_hal_spi_t *spi = (mp_hal_spi_t *)spi_obj;
    if (!spi->initialized) {
        mp_raise_ValueError("SPI not initialized");
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t tx_byte = tx_buf ? tx_buf[i] : 0xFF;
        uint8_t rx_byte = USART_SpiTransfer(spi->usart, tx_byte);
        if (rx_buf) {
            rx_buf[i] = rx_byte;
        }
    }
}

mp_hal_spi_obj_t mp_hal_spi_get(size_t id) {
    if (id >= SPI_MAX_INSTANCES) {
        mp_raise_ValueError("invalid SPI id");
    }
    return &spi_instances[id];
}

void mp_hal_spi_get_default_pins(size_t id, uint8_t *sck, uint8_t *mosi, uint8_t *miso) {
    (void)id;
    // EFR32MG21 breakout board SPI flash: SCK=PC02(8), MOSI=PC00(6), MISO=PC01(7).
    *sck = 8;
    *mosi = 6;
    *miso = 7;
}
