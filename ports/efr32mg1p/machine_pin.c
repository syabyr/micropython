/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * Development of the code in this file was sponsored by Microbric Pty Ltd
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

#include "py/runtime.h"
#include "py/mphal.h"
#include "mphalport.h"
#include "machine_pin.h"
#include "extmod/virtpin.h"

#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_MODE_INPUT_PULLUP 3

#define GPIO_PULL_UP 1
#define GPIO_PULL_DOWN 2
#define GPIO_IRQ_RISING 1
#define GPIO_IRQ_FALLING 2

static uint8_t pin_irq_ids[16];
MP_REGISTER_ROOT_POINTER(mp_obj_t pin_irq_handlers[16]);

// pin.init(mode, pull=None, *, value)
STATIC mp_obj_t machine_pin_obj_init_helper(mp_hal_pin_obj_t self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_pull, ARG_value };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode, MP_ARG_OBJ, {.u_obj = mp_const_none}},
        { MP_QSTR_pull, MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_SMALL_INT(-1)}},
        { MP_QSTR_value, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // set initial value (do this before configuring mode/pull)
    if (args[ARG_value].u_obj != MP_OBJ_NULL) {
        mp_hal_pin_write(self, mp_obj_is_true(args[ARG_value].u_obj));
    }

    // configure mode
    if (args[ARG_mode].u_obj != mp_const_none) {
        mp_int_t pin_io_mode = mp_obj_get_int(args[ARG_mode].u_obj);
        if (pin_io_mode == GPIO_MODE_OUTPUT) {
            mp_hal_pin_output(self);
        } else
        if (pin_io_mode == GPIO_MODE_INPUT) {
            mp_hal_pin_input(self);
        } else {
            mp_raise_ValueError("pin mode unknown");
        }
    }

    // configure pull
    if (args[ARG_pull].u_obj != mp_const_none
        && !(mp_obj_is_small_int(args[ARG_pull].u_obj) && mp_obj_get_int(args[ARG_pull].u_obj) == -1)) {
        int mode = mp_obj_get_int(args[ARG_pull].u_obj);
        if (mode == GPIO_PULL_UP) {
            GPIO_PinModeSet(self->port, self->pin, gpioModeInputPull, 1);
        } else if (mode == GPIO_PULL_DOWN) {
            GPIO_PinModeSet(self->port, self->pin, gpioModeInputPull, 0);
        } else {
            mp_raise_ValueError("pin pull unknown");
        }
    }

    return mp_const_none;
}

// constructor(id, ...)
mp_obj_t mp_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    // get the wanted pin object
    int wanted_pin = mp_obj_get_int(args[0]);
    mp_hal_pin_obj_t self = mp_hal_pin_lookup(wanted_pin);
    if (self == NULL) {
        mp_raise_ValueError("invalid pin");
    }

    if (n_args > 1 || n_kw > 0) {
        // pin mode given, so configure this GPIO
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        machine_pin_obj_init_helper(self, n_args - 1, args + 1, &kw_args);
    }

    return MP_OBJ_FROM_PTR(self);
}

void mp_pin_print(const mp_print_t * print, mp_obj_t self_in, mp_print_kind_t kind)
{
	mp_hal_pin_obj_t self = self_in;
	mp_printf(print, "Pin(%u)", mp_hal_pin_id(self));
}


// fast method for getting/setting pin value
STATIC mp_obj_t machine_pin_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    mp_hal_pin_obj_t self = self_in;
    if (n_args == 0) {
        // get pin
        return MP_OBJ_NEW_SMALL_INT(mp_hal_pin_read(self));
    } else {
        // set pin
        mp_hal_pin_write(self, mp_obj_is_true(args[0]));
        return mp_const_none;
    }
}

// pin.init(mode, pull)
STATIC mp_obj_t machine_pin_obj_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return machine_pin_obj_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_init_obj, 1, machine_pin_obj_init);

// pin.value([value])
STATIC mp_obj_t machine_pin_value(size_t n_args, const mp_obj_t *args) {
    return machine_pin_call(args[0], n_args - 1, 0, args + 1);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(machine_pin_value_obj, 1, 2, machine_pin_value);

// pin.off()
STATIC mp_obj_t machine_pin_off(mp_obj_t self_in) {
    mp_hal_pin_obj_t self = MP_OBJ_TO_PTR(self_in);
    mp_hal_pin_write(self, 0);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_off_obj, machine_pin_off);

// pin.on()
STATIC mp_obj_t machine_pin_on(mp_obj_t self_in) {
    mp_hal_pin_obj_t self = MP_OBJ_TO_PTR(self_in);
    mp_hal_pin_write(self, 1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_on_obj, machine_pin_on);

// pin.toggle()
STATIC mp_obj_t machine_pin_toggle(mp_obj_t self_in) {
    mp_hal_pin_obj_t self = MP_OBJ_TO_PTR(self_in);
    mp_hal_pin_write(self, !mp_hal_pin_read(self));
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_pin_toggle_obj, machine_pin_toggle);

STATIC mp_obj_t machine_pin_irq(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum { ARG_handler, ARG_trigger };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_handler, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_trigger, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = GPIO_IRQ_FALLING | GPIO_IRQ_RISING} },
    };
    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, vals);

    mp_hal_pin_obj_t self = MP_OBJ_TO_PTR(args[0]);
    unsigned int int_no = self->pin;
    if (int_no >= MP_ARRAY_SIZE(pin_irq_ids)) {
        mp_raise_ValueError("invalid IRQ pin");
    }

    if (vals[ARG_handler].u_obj == mp_const_none) {
        GPIO_ExtIntConfig(self->port, self->pin, int_no, false, false, false);
        GPIO_IntDisable(1u << int_no);
        pin_irq_ids[int_no] = 0;
        MP_STATE_PORT(pin_irq_handlers[int_no]) = mp_const_none;
        return mp_const_none;
    }

    if (pin_irq_ids[int_no] != 0 && pin_irq_ids[int_no] != self->gpio_id + 1) {
        mp_raise_ValueError("IRQ line already in use");
    }

    unsigned int trigger = vals[ARG_trigger].u_int;
    bool rising = trigger & GPIO_IRQ_RISING;
    bool falling = trigger & GPIO_IRQ_FALLING;
    if (!rising && !falling) {
        mp_raise_ValueError("invalid IRQ trigger");
    }

    pin_irq_ids[int_no] = self->gpio_id + 1;
    MP_STATE_PORT(pin_irq_handlers[int_no]) = vals[ARG_handler].u_obj;
    GPIO_IntClear(1u << int_no);
    GPIO_ExtIntConfig(self->port, self->pin, int_no, rising, falling, true);
    NVIC_ClearPendingIRQ((int_no & 1) ? GPIO_ODD_IRQn : GPIO_EVEN_IRQn);
    NVIC_EnableIRQ((int_no & 1) ? GPIO_ODD_IRQn : GPIO_EVEN_IRQn);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(machine_pin_irq_obj, 1, machine_pin_irq);

static void machine_pin_irq_handler(uint32_t flags) {
    GPIO_IntClear(flags);
    for (unsigned int int_no = 0; int_no < MP_ARRAY_SIZE(pin_irq_ids); ++int_no) {
        if ((flags & (1u << int_no)) && pin_irq_ids[int_no] != 0) {
            mp_obj_t handler = MP_STATE_PORT(pin_irq_handlers[int_no]);
            if (handler != mp_const_none) {
                mp_hal_pin_obj_t pin = mp_hal_pin_lookup(pin_irq_ids[int_no] - 1);
                mp_sched_schedule(handler, MP_OBJ_FROM_PTR(pin));
            }
        }
    }
}

void GPIO_EVEN_IRQHandler(void) {
    machine_pin_irq_handler(GPIO_IntGetEnabled() & 0x5555);
}

void GPIO_ODD_IRQHandler(void) {
    machine_pin_irq_handler(GPIO_IntGetEnabled() & 0xaaaa);
}


STATIC const mp_rom_map_elem_t machine_pin_locals_dict_table[] = {
    // instance methods
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_pin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&machine_pin_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&machine_pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&machine_pin_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_toggle), MP_ROM_PTR(&machine_pin_toggle_obj) },
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&machine_pin_irq_obj) },

    // class constants
    { MP_ROM_QSTR(MP_QSTR_IN), MP_ROM_INT(GPIO_MODE_INPUT) },
    { MP_ROM_QSTR(MP_QSTR_OUT), MP_ROM_INT(GPIO_MODE_OUTPUT) },
    { MP_ROM_QSTR(MP_QSTR_PULL_UP), MP_ROM_INT(GPIO_PULL_UP) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DOWN), MP_ROM_INT(GPIO_PULL_DOWN) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING), MP_ROM_INT(GPIO_IRQ_RISING) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING), MP_ROM_INT(GPIO_IRQ_FALLING) },
/*
    { MP_ROM_QSTR(MP_QSTR_OPEN_DRAIN), MP_ROM_INT(GPIO_MODE_INPUT_OUTPUT_OD) },
    { MP_ROM_QSTR(MP_QSTR_PULL_HOLD), MP_ROM_INT(GPIO_PULL_HOLD) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING), MP_ROM_INT(GPIO_PIN_INTR_POSEDGE) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING), MP_ROM_INT(GPIO_PIN_INTR_NEGEDGE) },
    { MP_ROM_QSTR(MP_QSTR_WAKE_LOW), MP_ROM_INT(GPIO_PIN_INTR_LOLEVEL) },
    { MP_ROM_QSTR(MP_QSTR_WAKE_HIGH), MP_ROM_INT(GPIO_PIN_INTR_HILEVEL) },
*/
};

STATIC mp_uint_t pin_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    (void)errcode;
    mp_hal_pin_obj_t self = self_in;

    switch (request) {
        case MP_PIN_READ: {
            return mp_hal_pin_read(self);
        }
        case MP_PIN_WRITE: {
            mp_hal_pin_write(self, arg);
            return 0;
        }
    }
    return -1;
}

STATIC MP_DEFINE_CONST_DICT(machine_pin_locals_dict, machine_pin_locals_dict_table);

STATIC const mp_pin_p_t pin_pin_p = {
  .ioctl = pin_ioctl,
};

// 新的类型定义方式，适配带slots的MicroPython版本
MP_DEFINE_CONST_OBJ_TYPE(
    machine_pin_type,
    MP_QSTR_Pin,
    MP_TYPE_FLAG_NONE,
    print, mp_pin_print,
    make_new, mp_pin_make_new,
    call, machine_pin_call,
    protocol, &pin_pin_p,
    locals_dict, &machine_pin_locals_dict
);

