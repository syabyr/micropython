#include <stdio.h>
#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/binary.h"
#include "py/mphal.h"
#include "mphalport.h"
#include "em_timer.h"

void mp_hal_pwm_init(void)
{
	static bool init_done;
	if (init_done)
		return;
	init_done = 1;

	// Set Top Value to 
	TIMER_TopSet(TIMER1, MP_HAL_PWM_TOP);

	// Create a timerInit object and set prescaler
	TIMER_Init_TypeDef timerInit = TIMER_INIT_DEFAULT;
	timerInit.prescale = timerPrescale4;
 
	TIMER_Init(TIMER1, &timerInit);
}


int mp_hal_pwm_freq_get(mp_hal_pwm_obj_t pwm)
{
	return -1;
}

void mp_hal_pwm_freq(mp_hal_pwm_obj_t pwm, int freq)
{
	mp_raise_ValueError("PWM.Freq() not supported");
}

int mp_hal_pwm_duty_get(mp_hal_pwm_obj_t pwm)
{
	if (pwm->pin->pwm_config == 0xFF)
		mp_raise_ValueError("PWM not supported");

	const unsigned channel = (pwm->pin->pwm_config >> 4) & 0xF;
	//const unsigned location = (pin->pwm_config >> 0) & 0xF;
	return TIMER1->CC[channel].CCV;
}

void mp_hal_pwm_duty(mp_hal_pwm_obj_t pwm, int duty)
{
	const uint8_t pwm_config = pwm->pin->pwm_config;
	if (pwm_config == 0xFF)
		mp_raise_ValueError("PWM not supported");

	const unsigned channel = (pwm_config >> 4) & 0xF;
	const unsigned location = (pwm_config >> 0) & 0xF;

	if (!pwm->active)
	{
		pwm->active = 1;

		// Create the timer count control object initializer
		TIMER_InitCC_TypeDef timerCCInit = TIMER_INITCC_DEFAULT;
		timerCCInit.mode = timerCCModePWM;
		timerCCInit.cmoa = timerOutputActionToggle;
	 
		// Configure CC channel
		TIMER_InitCC(TIMER1, channel, &timerCCInit);
	 
		/* Route CC1 to location and enable pin;
		 * values from efm32/gecko_sdk/include/efr32mg1p_timer.h
		 * #define TIMER_ROUTEPEN_CC0PEN (0x1UL << 0)
		 */
		TIMER1->ROUTEPEN |= (0x1UL << channel);
		TIMER1->ROUTELOC0 |= (location << (8*channel));
	}
 
	TIMER_CompareBufSet(TIMER1, channel, duty);
}

void mp_hal_pwm_deinit(mp_hal_pwm_obj_t pwm)
{
	if (!pwm->active)
		return;
	pwm->active = 0;

	const uint8_t pwm_config = pwm->pin->pwm_config;
	if (pwm_config == 0xFF)
		return;

	const unsigned channel = (pwm_config >> 4) & 0xF;
	//const unsigned location = (pwm_config >> 0) & 0xF;

	// Reset the timer count control object initializer to default
	TIMER_InitCC_TypeDef timerCCInit = TIMER_INITCC_DEFAULT;
	TIMER_InitCC(TIMER1, channel, &timerCCInit);
	 
	/* Disable route CC1 to location and enable pin;
	 * values from efm32/gecko_sdk/include/efr32mg1p_timer.h
	 */
	TIMER1->ROUTEPEN |= (0x0UL << channel);
}


static void mp_machine_pwm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {

}


mp_obj_t mp_machine_pwm_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *args) {
		//return MP_OBJ_FROM_PTR(self);
		return NULL;
	}


static void mp_machine_pwm_init_helper(machine_pwm_obj_t *self,
    size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

}


// This called from pwm.deinit() method
static void mp_machine_pwm_deinit(machine_pwm_obj_t *self) {
}


static mp_obj_t mp_machine_pwm_freq_get(machine_pwm_obj_t *self) {
    //return MP_OBJ_NEW_SMALL_INT(ledc_get_freq(self->mode, self->timer));
	return MP_OBJ_NEW_SMALL_INT(0);
}

static void mp_machine_pwm_freq_set(machine_pwm_obj_t *self, mp_int_t freq)
{

}

static uint32_t get_duty_u16(machine_pwm_obj_t *self) {
    //return ledc_get_duty(self->mode, self->channel) << (HIGHEST_PWM_RES + UI_RES_SHIFT - timers[TIMER_IDX(self->mode, self->timer)].duty_resolution);
	return 0;
}

static uint32_t get_duty_u10(machine_pwm_obj_t *self) {
    //return ledc_get_duty(self->mode, self->channel) << (HIGHEST_PWM_RES + UI_RES_SHIFT - timers[TIMER_IDX(self->mode, self->timer)].duty_resolution);
	return 0;
}

static mp_obj_t mp_machine_pwm_duty_get(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT(get_duty_u10(self));
}

static void mp_machine_pwm_duty_set(machine_pwm_obj_t *self, mp_int_t duty) {
    //set_duty_u10(self, duty);
}

static uint32_t get_duty_ns(machine_pwm_obj_t *self) {
    //return duty_to_ns(self, get_duty_u16(self));
	return 0;
}

static mp_obj_t mp_machine_pwm_duty_get_u16(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT(get_duty_u16(self));
}

static void mp_machine_pwm_duty_set_u16(machine_pwm_obj_t *self, mp_int_t duty_u16) {
    //set_duty_u16(self, duty_u16);
}

static mp_obj_t mp_machine_pwm_duty_get_ns(machine_pwm_obj_t *self) {
    return MP_OBJ_NEW_SMALL_INT(get_duty_ns(self));
}

static void mp_machine_pwm_duty_set_ns(machine_pwm_obj_t *self, mp_int_t duty_ns) {
    //set_duty_ns(self, duty_ns);
}

