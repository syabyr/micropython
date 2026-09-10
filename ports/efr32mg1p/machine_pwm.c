#include "py/runtime.h"
#include "py/mphal.h"
#include "mphalport.h"
#include "machine_pin.h"

#define VALUE_NOT_SET (-1)

typedef struct _machine_pwm_obj_t {
    mp_obj_base_t base;
    mp_hal_pwm_obj_t pwm;
    uint16_t duty_u16;
} machine_pwm_obj_t;

static void mp_machine_pwm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    machine_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "PWM(Pin(%u), freq=%u, duty_u16=%u)", mp_hal_pin_id(self->pwm->pin), mp_hal_pwm_freq_get(self->pwm), self->duty_u16);
}

static void mp_machine_pwm_duty_set_u16(machine_pwm_obj_t *self, mp_int_t duty_u16);

static void mp_machine_pwm_init_helper(machine_pwm_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_freq, ARG_duty_u16, ARG_duty_ns };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = VALUE_NOT_SET} },
        { MP_QSTR_duty_u16, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = VALUE_NOT_SET} },
        { MP_QSTR_duty_ns, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = VALUE_NOT_SET} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_freq].u_int != VALUE_NOT_SET) {
        mp_hal_pwm_freq(self->pwm, args[ARG_freq].u_int);
    }
    if (args[ARG_duty_ns].u_int != VALUE_NOT_SET) {
        mp_raise_ValueError(MP_ERROR_TEXT("PWM duty_ns not supported"));
    }
    if (args[ARG_duty_u16].u_int != VALUE_NOT_SET) {
        mp_machine_pwm_duty_set_u16(self, args[ARG_duty_u16].u_int);
    }
}

static mp_obj_t mp_machine_pwm_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    mp_hal_pin_obj_t pin;
    if (mp_obj_is_type(args[0], &machine_pin_type)) {
        pin = MP_OBJ_TO_PTR(args[0]);
    } else {
        pin = mp_hal_pin_lookup(mp_obj_get_int(args[0]));
        if (pin == NULL) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid pin"));
        }
    }
    if (pin->pwm_config == 0xFF) {
        mp_raise_ValueError(MP_ERROR_TEXT("PWM not supported"));
    }

    machine_pwm_obj_t *self = mp_obj_malloc(machine_pwm_obj_t, type);
    self->base.type = type;
    self->pwm = mp_obj_malloc(struct _mp_hal_pwm_t, NULL);
    self->pwm->base.type = NULL;
    self->pwm->pin = pin;
    self->pwm->active = 0;
    self->duty_u16 = 0;

    mp_hal_pwm_init();
    mp_machine_pwm_init_helper(self, n_args - 1, args + 1, (mp_map_t *)&mp_const_empty_map);
    return MP_OBJ_FROM_PTR(self);
}

static void mp_machine_pwm_deinit(machine_pwm_obj_t *self) {
    mp_hal_pwm_deinit(self->pwm);
}

static mp_obj_t mp_machine_pwm_freq_get(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT(mp_hal_pwm_freq_get(self->pwm));
}

static void mp_machine_pwm_freq_set(machine_pwm_obj_t *self, mp_int_t freq) {
    mp_hal_pwm_freq(self->pwm, freq);
    mp_machine_pwm_duty_set_u16(self, self->duty_u16);
}

#if MICROPY_PY_MACHINE_PWM_DUTY
static mp_obj_t mp_machine_pwm_duty_get(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT(mp_hal_pwm_duty_get(self->pwm));
}

static void mp_machine_pwm_duty_set(machine_pwm_obj_t *self, mp_int_t duty) {
    int pwm_top = mp_hal_pwm_top_get();
    if (duty < 0) {
        duty = 0;
    } else if (duty > pwm_top) {
        duty = pwm_top;
    }
    self->duty_u16 = duty * 65535 / pwm_top;
    mp_hal_pwm_duty(self->pwm, duty);
}
#endif

static mp_obj_t mp_machine_pwm_duty_get_u16(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT(self->duty_u16);
}

static void mp_machine_pwm_duty_set_u16(machine_pwm_obj_t *self, mp_int_t duty_u16) {
    if (duty_u16 < 0) {
        duty_u16 = 0;
    } else if (duty_u16 > 65535) {
        duty_u16 = 65535;
    }
    self->duty_u16 = duty_u16;
    mp_hal_pwm_duty(self->pwm, duty_u16 * mp_hal_pwm_top_get() / 65535);
}

static mp_obj_t mp_machine_pwm_duty_get_ns(machine_pwm_obj_t *self) {
    (void)self;
    mp_raise_ValueError(MP_ERROR_TEXT("PWM duty_ns not supported"));
}

static void mp_machine_pwm_duty_set_ns(machine_pwm_obj_t *self, mp_int_t duty_ns) {
    (void)self;
    (void)duty_ns;
    mp_raise_ValueError(MP_ERROR_TEXT("PWM duty_ns not supported"));
}
