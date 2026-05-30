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
#include <stdio.h>
#include <string.h>
#include "py/runtime.h"
#include "py/mphal.h"
#include "em_usart.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "machine_spi.h"
#include "mphalport.h"
#define SPI_MAX_INSTANCES 2 // 支持2路硬件SPI
// SPI实例结构体
typedef struct _mp_hal_spi_t {
    USART_TypeDef *usart;
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    uint8_t firstbit;
    uint8_t sck; // sck引脚编号
    uint8_t mosi; // mosi引脚编号
    uint8_t miso; // miso引脚编号
    bool initialized;
} mp_hal_spi_t;
static mp_hal_spi_t spi_instances[SPI_MAX_INSTANCES] = {
    {USART0, 1000000, 0, 0, 8, 0, 0, 0, 0, false}, // SPI0默认用USART0
    {USART1, 1000000, 0, 0, 8, 0, 0, 0, 0, false}, // SPI1默认用USART1
};
// 初始化SPI
void mp_hal_spi_init(mp_hal_spi_obj_t spi_obj, uint32_t baudrate, uint8_t polarity, uint8_t phase, uint8_t bits, uint8_t firstbit, uint8_t sck, uint8_t mosi, uint8_t miso) {
    mp_hal_spi_t *spi = (mp_hal_spi_t *)spi_obj;
    // 保存配置
    spi->baudrate = baudrate;
    spi->polarity = polarity;
    spi->phase = phase;
    spi->bits = bits;
    spi->firstbit = firstbit;
    spi->sck = sck;
    spi->mosi = mosi;
    spi->miso = miso;
    // 启用USART时钟
    if (spi->usart == USART0) {
        CMU_ClockEnable(cmuClock_USART0, true);
    } else if (spi->usart == USART1) {
        CMU_ClockEnable(cmuClock_USART1, true);
    }
    // 获取引脚对应的端口和引脚号
    mp_hal_pin_obj_t sck_pin = mp_hal_pin_lookup(sck);
    mp_hal_pin_obj_t mosi_pin = mp_hal_pin_lookup(mosi);
    mp_hal_pin_obj_t miso_pin = mp_hal_pin_lookup(miso);
    if (!sck_pin || !mosi_pin || !miso_pin) {
        mp_raise_ValueError("invalid SPI pin");
    }
    // 配置SCK和MOSI为推挽输出
    GPIO_PinModeSet(sck_pin->port, sck_pin->pin, gpioModePushPull, polarity);
    GPIO_PinModeSet(mosi_pin->port, mosi_pin->pin, gpioModePushPull, 0);
    // 配置MISO为输入
    GPIO_PinModeSet(miso_pin->port, miso_pin->pin, gpioModeInput, 0);
    // 配置USART为SPI主模式
    USART_InitSync_TypeDef init = USART_INITSYNC_DEFAULT;
    init.baudrate = baudrate;
    init.databits = usartDatabits8; // 默认8位
    init.master = true; // 主模式
    init.msbf = (firstbit == 0); // MSB优先
    init.clockMode = polarity | (phase << 1); // CPOL和CPHA组合
    init.autoCsEnable = false; // 不使用硬件片选
    // 初始化USART
    USART_InitSync(spi->usart, &init);
    // 配置引脚路由
    // 根据引脚确定路由位置，这里简化处理，默认用位置0
    if (spi->usart == USART0) {
        // USART0位置0: CLK=PA2, TX=PA0, RX=PA1
        spi->usart->ROUTELOC0 = USART_ROUTELOC0_CLKLOC_LOC0 |
                                USART_ROUTELOC0_TXLOC_LOC0 |
                                USART_ROUTELOC0_RXLOC_LOC0;
    } else if (spi->usart == USART1) {
        // USART1位置0: CLK=PC15, TX=PC14, RX=PC13
        spi->usart->ROUTELOC0 = USART_ROUTELOC0_CLKLOC_LOC0 |
                                USART_ROUTELOC0_TXLOC_LOC0 |
                                USART_ROUTELOC0_RXLOC_LOC0;
    }
    // 启用USART引脚
    spi->usart->ROUTEPEN = USART_ROUTEPEN_CLKPEN | USART_ROUTEPEN_TXPEN | USART_ROUTEPEN_RXPEN;
    spi->initialized = true;
}
// 反初始化SPI
void mp_hal_spi_deinit(mp_hal_spi_obj_t spi_obj) {
    mp_hal_spi_t *spi = (mp_hal_spi_t *)spi_obj;
    if (!spi->initialized) return;
    // 禁用USART
    USART_Enable(spi->usart, usartDisable);
    // 关闭时钟
    if (spi->usart == USART0) {
        CMU_ClockEnable(cmuClock_USART0, false);
    } else if (spi->usart == USART1) {
        CMU_ClockEnable(cmuClock_USART1, false);
    }
    // 复位引脚为输入
    mp_hal_pin_obj_t sck_pin = mp_hal_pin_lookup(spi->sck);
    mp_hal_pin_obj_t mosi_pin = mp_hal_pin_lookup(spi->mosi);
    mp_hal_pin_obj_t miso_pin = mp_hal_pin_lookup(spi->miso);
    if (sck_pin) mp_hal_pin_input(sck_pin);
    if (mosi_pin) mp_hal_pin_input(mosi_pin);
    if (miso_pin) mp_hal_pin_input(miso_pin);
    spi->initialized = false;
}
// SPI传输
void mp_hal_spi_transfer(mp_hal_spi_obj_t spi_obj, size_t len, const uint8_t *tx_buf, uint8_t *rx_buf) {
    mp_hal_spi_t *spi = (mp_hal_spi_t *)spi_obj;
    if (!spi->initialized) mp_raise_ValueError("SPI not initialized");
    for (size_t i = 0; i < len; i++) {
        uint8_t tx_byte = tx_buf ? tx_buf[i] : 0xFF;
        uint8_t rx_byte = USART_SpiTransfer(spi->usart, tx_byte);
        if (rx_buf) {
            rx_buf[i] = rx_byte;
        }
    }
}
// 获取SPI实例
mp_hal_spi_obj_t mp_hal_spi_get(size_t id) {
    if (id >= SPI_MAX_INSTANCES) {
        mp_raise_ValueError("invalid SPI id");
    }
    return &spi_instances[id];
}
// 获取SPI默认引脚配置
void mp_hal_spi_get_default_pins(size_t id, uint8_t *sck, uint8_t *mosi, uint8_t *miso) {
    // IKEA板子默认SPI引脚: SCK=13(PD13), MOSI=15(PD15), MISO=14(PD14)
    *sck = 13;
    *mosi = 15;
    *miso = 14;
}
