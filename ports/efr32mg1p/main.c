/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Damien P. George
 * Copyright (c) 2019 Trammell Hudson
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
#include <stdio.h>
#include <string.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/mperrno.h"
#include "py/builtin.h"
#include "drivers/bus/spi.h"
#include "shared/runtime/pyexec.h"
#include "extmod/vfs.h"
#include "extmod/vfs_lfs.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "em_usart.h"
#include "zrepl.h"
#include "radio.h"
#include "machine_spi.h"
#include "machine_spiflash.h"
#include "genhdr/mpversion.h"

#if MICROPY_VFS
MP_REGISTER_ROOT_POINTER(struct _mp_vfs_mount_t *vfs_cur);
MP_REGISTER_ROOT_POINTER(struct _mp_vfs_mount_t *vfs_mount_table);
#endif

extern uint8_t __StackTop;
extern uint8_t __HeapBase;
extern uint8_t __HeapLimit;
int *__errno (void)
{
	static int _errno;
	return &_errno;
}

#if MICROPY_VFS && MICROPY_VFS_LFS2
#define EFR32_SPIFLASH_SPI_ID (0)
#define EFR32_SPIFLASH_CS_PIN (12)
#define EFR32_SPIFLASH_SCK_PIN (13)
#define EFR32_SPIFLASH_MISO_PIN (14)
#define EFR32_SPIFLASH_MOSI_PIN (15)

MP_NOINLINE static bool init_flash_fs(void) {
	nlr_buf_t nlr;
	if (nlr_push(&nlr) != 0) {
		printf("MPY: flash fs init exception\n");
		return false;
	}

	machine_spiflash_obj_t *bdev = mp_obj_malloc(machine_spiflash_obj_t, &machine_spiflash_type);
	static mp_soft_spi_obj_t soft_spi;
	bdev->spi = &soft_spi;
	bdev->spi_proto = &mp_soft_spi_proto;
	bdev->cs = mp_hal_pin_lookup(EFR32_SPIFLASH_CS_PIN);
	if (bdev->cs == NULL) {
		printf("MPY: invalid SPI flash CS pin\n");
		nlr_pop();
		return false;
	}

	soft_spi.delay_half = 1;
	soft_spi.polarity = 0;
	soft_spi.phase = 0;
	soft_spi.firstbit = MICROPY_PY_MACHINE_SPI_MSB;
	soft_spi.sck = mp_hal_pin_lookup(EFR32_SPIFLASH_SCK_PIN);
	soft_spi.mosi = mp_hal_pin_lookup(EFR32_SPIFLASH_MOSI_PIN);
	soft_spi.miso = mp_hal_pin_lookup(EFR32_SPIFLASH_MISO_PIN);
	if (soft_spi.sck == NULL || soft_spi.mosi == NULL || soft_spi.miso == NULL) {
		printf("MPY: invalid SPI flash pin\n");
		nlr_pop();
		return false;
	}
	mp_soft_spi_ioctl(&soft_spi, MP_SPI_IOCTL_INIT);

	mp_hal_pin_output(bdev->cs);
	mp_hal_pin_write(bdev->cs, 1);

	mp_obj_t bdev_obj = MP_OBJ_FROM_PTR(bdev);
	mp_obj_t mount_point = MP_OBJ_NEW_QSTR(MP_QSTR__slash_);
	int ret = mp_vfs_mount_and_chdir_protected(bdev_obj, mount_point);
	if (ret == -MP_ENODEV) {
		// First boot or empty flash: create LFS2 then mount again.
		nlr_buf_t nlr;
		if (nlr_push(&nlr) == 0) {
			mp_obj_t mkfs_args[3];
			mp_load_method(MP_OBJ_FROM_PTR(&mp_type_vfs_lfs2), MP_QSTR_mkfs, mkfs_args);
			mkfs_args[2] = bdev_obj;
			mp_call_method_n_kw(1, 0, mkfs_args);
			nlr_pop();
		} else {
			printf("MPY: mkfs failed\n");
			return false;
		}
		ret = mp_vfs_mount_and_chdir_protected(bdev_obj, mount_point);
	}

	if (ret != 0) {
		printf("MPY: can't mount SPI flash (%d)\n", ret);
		nlr_pop();
		return false;
	}
	nlr_pop();
	return true;
}
#endif

int main(int argc, char **argv)
{
	// must be called immediately in main() to handle errata
	CHIP_Init();

	// mbed-os does this?
	EMU_DCDCInit_TypeDef dcdcInit = EMU_DCDCINIT_DEFAULT;
	EMU_DCDCInit(&dcdcInit);

	// init the 38.4 MHz high frequency clock for the radio
	CMU_HFXOInit_TypeDef hfxoInit = CMU_HFXOINIT_DEFAULT;
	CMU_HFXOInit(&hfxoInit);

	// Switch HFCLK to HFXO and disable HFRCO
	CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO);
	//SystemHFXOClockSet(EFR32_HFXO_FREQ); //should already be at 38.4 MHz
	//CMU_OscillatorEnable(cmuOsc_HFRCO, false, false);
	CMU_ClockEnable(cmuClock_CORELE, true);

	/* Turn RTCC clock gate back on to keep RTC time correct */
	CMU_ClockEnable(cmuClock_RTCC, true);

	/* Enable AES clock: otherwise result from AES ops is 0 */
	CMU_ClockEnable(cmuClock_CRYPTO, true);

	/* Enable clock for GPIO module and TIMER1 for PWM */
	CMU_ClockEnable(cmuClock_GPIO, true);
	CMU_ClockEnable(cmuClock_TIMER1, true );

	/* TODO: figure out if we can turn off clocks to save power */

	extern void mp_hal_stdout_init(void);
	extern int mp_hal_stdin_rx_chr(void);
	mp_hal_stdout_init();

	// send a quick broadcast packet to announce the device
	radio_init();
	zrepl_active = 1;
	static const char version[] = "MicroPython " MICROPY_GIT_TAG " on " MICROPY_BUILD_DATE;
	zrepl_send(version, sizeof(version) - 1);
	zrepl_active = 0;

soft_reset:
	if (1)
	{
		// should do something there
	}

    //gc_init(heap, heap + sizeof(heap));
    uint8_t * const heap_base = &__HeapBase;
    size_t heap_size = &__HeapLimit - heap_base;
    uint8_t * const stack_top = &__StackTop;
    size_t stack_size = stack_top - &__HeapLimit;
    //printf("heap=%p + %08x stack=%p - %08x\n", heap_base, heap_size, stack_top, stack_size);
    //mp_pystack_init(&__HeapLimit, &__StackTop);
    mp_stack_set_top(stack_top);
    mp_stack_set_limit(stack_size);
    gc_init(heap_base, heap_base + heap_size);
    mp_init();

	#if MICROPY_VFS && MICROPY_VFS_LFS2
	init_flash_fs();
	#endif

    // run boot-up scripts
    //pyexec_frozen_module("__init__.py"); // 暂时注释掉，因为我们没有frozen模块了
    pyexec_file_if_exists("boot.py");
    if (pyexec_mode_kind == PYEXEC_MODE_FRIENDLY_REPL) {
        pyexec_file_if_exists("main.py");
    }

    for (;;) {
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            if (pyexec_raw_repl() != 0) {
                break;
            }
        } else {
            if (pyexec_friendly_repl() != 0) {
                break;
            }
        }
    }

    gc_sweep_all();
    printf("MPY: soft reboot\n");
    mp_deinit();
    goto soft_reset;

    return 0;
}

void _start(void)
{
	main(0, NULL);
}

static void uart_str(const char * str)
{
	while(*str)
		USART_Tx(USART1, *str++);
}

static void uart_u32(const unsigned val)
{
	static const char hexdigit[] = "0123456789abcdef";
	USART_Tx(USART1, hexdigit[(val >> 28) & 0xF]);
	USART_Tx(USART1, hexdigit[(val >> 24) & 0xF]);
	USART_Tx(USART1, hexdigit[(val >> 20) & 0xF]);
	USART_Tx(USART1, hexdigit[(val >> 16) & 0xF]);
	USART_Tx(USART1, hexdigit[(val >> 12) & 0xF]);
	USART_Tx(USART1, hexdigit[(val >>  8) & 0xF]);
	USART_Tx(USART1, hexdigit[(val >>  4) & 0xF]);
	USART_Tx(USART1, hexdigit[(val >>  0) & 0xF]);
}



void gc_collect(void) {
    // WARNING: This gc_collect implementation doesn't try to get root
    // pointers from CPU registers, and thus may function incorrectly.
    void *dummy;
    gc_collect_start();
    gc_collect_root(&dummy, ((mp_uint_t)&__StackTop - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
    gc_collect_end();
    //gc_dump_info();
}

static void __attribute__((__noreturn__))
reboot_delay(void)
{
	for(int i = 0 ; i < (1 <<28) ; i++)
		__asm__ __volatile__("nop");

	NVIC_SystemReset();
	while(1)
		;
}

void
nlr_jump_fail(void *val)
{
	uart_str("nlr_jump_fail: ");
	uart_u32((uint32_t) val);
	uart_str(" !!!\r\n");

	reboot_delay();
}

void NORETURN __fatal_error(const char *msg) {
	uart_str("!!! FATAL ERROR: ");
	uart_str(msg);
	
	reboot_delay();
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    __fatal_error("Assertion failed");
}
#endif

void NMI_Handler         (void) { uart_str(__func__); while(1); }
void MemManage_Handler   (void) { uart_str(__func__); while(1); }
void BusFault_Handler    (void) { uart_str(__func__); while(1); }
void UsageFault_Handler  (void) { uart_str(__func__); while(1); }
void SVC_Handler         (void) { uart_str(__func__); while(1); }
void PendSV_Handler      (void) { uart_str(__func__); while(1); }
void SysTick_Handler     (void) { uart_str(__func__); while(1); }


/*
 * During a hard fault r0, r1, r2, r3, r12, lr, pc, psr are
 * pushed onto the stack. the other registers are preserved.
 *
 * Really should print the faulting PC
 */
void HardFault_Handler   (void) __attribute__((__naked__));
void HardFault_Handler   (void)
{
	// read MMFAR in GDB: x/xw 0xE000ED34
	//const uint32_t fault_address = SCB->MMFAR;
	//const uint32_t fault_register = SCB->CFSR;

	uart_str("!!!!!\r\n");

	while(1)
	{
		// print it char by char
		uart_str(__func__);
		USART_Tx(USART1, ':');

		uart_u32(SCB->CFSR);
		if (SCB->CFSR & SCB_CFSR_BUSFAULTSR_Msk)
		{
			uart_str(" Bus=");
			uart_u32(SCB->BFAR);
		}

		if (SCB->CFSR & SCB_CFSR_MEMFAULTSR_Msk)
		{
			uart_str(" Mem=");
			uart_u32(SCB->MMFAR);
		}

		USART_Tx(USART1, '\r');
		USART_Tx(USART1, '\n');

		reboot_delay();
	}
}
