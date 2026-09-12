#ifndef MICROPY_INCLUDED_EFR32MG21_MPHALPORT_H
#define MICROPY_INCLUDED_EFR32MG21_MPHALPORT_H

#include "py/obj.h"
#include "mpconfigport.h"
#include "shared/runtime/interrupt_char.h"
#include "em_gpio.h"

#define mp_hal_delay_us_fast mp_hal_delay_us

// gpio functions
struct _mp_hal_pin_t {
    mp_obj_base_t base;     // MicroPython object header, must be first
    GPIO_Port_TypeDef port; // native GPIO port type
    unsigned pin;           // pin number, 0-15
    uint8_t gpio_id;        // logical pin id, matches Pin(n)
    uint8_t pwm_config;     // PWM config, 0xFF == no PWM
};

extern mp_hal_pin_obj_t mp_hal_pin_lookup(unsigned pin_id);
extern unsigned mp_hal_pin_id(mp_hal_pin_obj_t pin);
extern void mp_hal_pin_input(mp_hal_pin_obj_t pin);
extern void mp_hal_pin_output(mp_hal_pin_obj_t pin);
extern void mp_hal_pin_open_drain(mp_hal_pin_obj_t pin);
extern int mp_hal_pin_read(mp_hal_pin_obj_t pin);
extern void mp_hal_pin_write(mp_hal_pin_obj_t pin, int value);

#endif // MICROPY_INCLUDED_EFR32MG21_MPHALPORT_H
