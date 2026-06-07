/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
 *
 * The MIT License (MIT)
 *
 * Portions Copyright (c) 2016 Damien P. George
 * Copyright (c) 2020 Trammell Hudson
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
#include <sys/time.h>

#include "py/mphal.h"
#include "py/runtime.h"
#include "py/smallint.h"
#include "utime_mphal.h"

#include "rail.h"

unsigned mp_hal_ticks_cpu(void)
{
	return SysTick->VAL;
}

unsigned mp_hal_ticks_us(void)
{
	return RAIL_GetTime();
}

unsigned mp_hal_ticks_ms(void)
{
	return mp_hal_ticks_us() / 1000;
}

uint64_t mp_hal_time_ns(void)
{
	// no RTC, so just ticks * 1000
	return mp_hal_ticks_us() * 1000;
}

void mp_hal_delay_us(mp_uint_t us)
{
	unsigned now = mp_hal_ticks_us();
	unsigned end = now + us;
	unsigned last_poll = now;
	if (end < now)
	{
		// timer must wrap, wait for the wrap to happen
		while (mp_hal_ticks_us() > now) {
			unsigned t = mp_hal_ticks_us();
			if ((unsigned)(t - last_poll) >= 1000) {
				mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
				last_poll = t;
			}
		}
	}

	while (mp_hal_ticks_us() < end) {
		unsigned t = mp_hal_ticks_us();
		if ((unsigned)(t - last_poll) >= 1000) {
			mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
			last_poll = t;
		}
	}
}

void mp_hal_delay_ms(mp_uint_t ms)
{
	while (ms > 0) {
		mp_uint_t chunk_ms = ms > 1000 ? 1000 : ms;
		mp_hal_delay_us(chunk_ms * 1000);
		ms -= chunk_ms;
	}
}

static mp_obj_t utime_sleep(mp_obj_t seconds_o) {
#if MICROPY_PY_BUILTINS_FLOAT
	mp_hal_delay_ms((mp_uint_t)(1000 * mp_obj_get_float(seconds_o)));
#else
	mp_hal_delay_ms(1000 * mp_obj_get_int(seconds_o));
#endif
	return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mp_utime_sleep_obj, utime_sleep);

static mp_obj_t utime_sleep_ms(mp_obj_t arg) {
	mp_int_t ms = mp_obj_get_int(arg);
	if (ms >= 0) {
		mp_hal_delay_ms(ms);
	}
	return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mp_utime_sleep_ms_obj, utime_sleep_ms);

static mp_obj_t utime_sleep_us(mp_obj_t arg) {
	mp_int_t us = mp_obj_get_int(arg);
	if (us > 0) {
		mp_hal_delay_us(us);
	}
	return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mp_utime_sleep_us_obj, utime_sleep_us);

static mp_obj_t utime_ticks_ms(void) {
	return MP_OBJ_NEW_SMALL_INT(mp_hal_ticks_ms() & (MICROPY_PY_TIME_TICKS_PERIOD - 1));
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_utime_ticks_ms_obj, utime_ticks_ms);

static mp_obj_t utime_ticks_us(void) {
	return MP_OBJ_NEW_SMALL_INT(mp_hal_ticks_us() & (MICROPY_PY_TIME_TICKS_PERIOD - 1));
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_utime_ticks_us_obj, utime_ticks_us);

static mp_obj_t utime_ticks_cpu(void) {
	return MP_OBJ_NEW_SMALL_INT(mp_hal_ticks_cpu() & (MICROPY_PY_TIME_TICKS_PERIOD - 1));
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_utime_ticks_cpu_obj, utime_ticks_cpu);

static mp_obj_t utime_ticks_diff(mp_obj_t end_in, mp_obj_t start_in) {
	mp_uint_t start = MP_OBJ_SMALL_INT_VALUE(start_in);
	mp_uint_t end = MP_OBJ_SMALL_INT_VALUE(end_in);
	mp_int_t diff = ((end - start + MICROPY_PY_TIME_TICKS_PERIOD / 2) & (MICROPY_PY_TIME_TICKS_PERIOD - 1))
		- MICROPY_PY_TIME_TICKS_PERIOD / 2;
	return MP_OBJ_NEW_SMALL_INT(diff);
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_utime_ticks_diff_obj, utime_ticks_diff);

static mp_obj_t utime_ticks_add(mp_obj_t ticks_in, mp_obj_t delta_in) {
	mp_uint_t ticks = MP_OBJ_SMALL_INT_VALUE(ticks_in);
	mp_uint_t delta = mp_obj_get_int(delta_in);

	if (delta + MICROPY_PY_TIME_TICKS_PERIOD / 2 - 1 >= MICROPY_PY_TIME_TICKS_PERIOD - 1) {
		mp_raise_msg(&mp_type_OverflowError, MP_ERROR_TEXT("ticks interval overflow"));
	}

	return MP_OBJ_NEW_SMALL_INT((ticks + delta) & (MICROPY_PY_TIME_TICKS_PERIOD - 1));
}
MP_DEFINE_CONST_FUN_OBJ_2(mp_utime_ticks_add_obj, utime_ticks_add);

STATIC const mp_rom_map_elem_t time_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_utime) },

    { MP_ROM_QSTR(MP_QSTR_sleep), MP_ROM_PTR(&mp_utime_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&mp_utime_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_us), MP_ROM_PTR(&mp_utime_sleep_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&mp_utime_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_us), MP_ROM_PTR(&mp_utime_ticks_us_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_cpu), MP_ROM_PTR(&mp_utime_ticks_cpu_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_add), MP_ROM_PTR(&mp_utime_ticks_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_diff), MP_ROM_PTR(&mp_utime_ticks_diff_obj) },
};

STATIC MP_DEFINE_CONST_DICT(time_module_globals, time_module_globals_table);

const mp_obj_module_t utime_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&time_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_utime, utime_module);
