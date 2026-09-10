#include <stdio.h>
#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/binary.h"
#include "py/mphal.h"
#include "mphalport.h"
#include "machine_pin.h"
#include "em_timer.h"
#include "em_cmu.h"

static bool pwm_init_done;
static unsigned pwm_active_channels;
static int pwm_freq = 1000;
static uint32_t pwm_top = MP_HAL_PWM_TOP;

static void mp_hal_pwm_config_freq(int freq)
{
    if (freq <= 0) {
        mp_raise_ValueError("invalid PWM frequency");
    }

    static const TIMER_Prescale_TypeDef prescalers[] = {
        timerPrescale1, timerPrescale2, timerPrescale4, timerPrescale8,
        timerPrescale16, timerPrescale32, timerPrescale64, timerPrescale128,
        timerPrescale256, timerPrescale512, timerPrescale1024,
    };
    static const uint16_t divs[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 };

    uint32_t top = 0;
    TIMER_Prescale_TypeDef prescale = timerPrescale1;
    for (size_t i = 0; i < MP_ARRAY_SIZE(divs); ++i) {
        uint32_t timer_freq = CMU_ClockFreqGet(cmuClock_TIMER1) / divs[i];
        top = timer_freq / freq;
        if (top > 0 && top <= 0xffff) {
            prescale = prescalers[i];
            break;
        }
    }

    if (top == 0 || top > 0xffff) {
        mp_raise_ValueError("unsupported PWM frequency");
    }

    TIMER_Enable(TIMER1, false);
    TIMER_Init_TypeDef timerInit = TIMER_INIT_DEFAULT;
    timerInit.prescale = prescale;
    TIMER_Init(TIMER1, &timerInit);
    TIMER_TopSet(TIMER1, top);
    TIMER_Enable(TIMER1, true);
    pwm_freq = freq;
    pwm_top = top;
}

void mp_hal_pwm_init(void)
{
	if (pwm_init_done)
		return;
	pwm_init_done = 1;
	CMU_ClockEnable(cmuClock_TIMER1, true);
    mp_hal_pwm_config_freq(pwm_freq);
}


int mp_hal_pwm_freq_get(mp_hal_pwm_obj_t pwm)
{
    (void)pwm;
	return pwm_freq;
}

void mp_hal_pwm_freq(mp_hal_pwm_obj_t pwm, int freq)
{
    (void)pwm;
    if (!pwm_init_done) {
        mp_hal_pwm_init();
    }
    mp_hal_pwm_config_freq(freq);
}

int mp_hal_pwm_duty_get(mp_hal_pwm_obj_t pwm)
{
	if (pwm->pin->pwm_config == 0xFF)
		mp_raise_ValueError("PWM not supported");

	const unsigned channel = (pwm->pin->pwm_config >> 4) & 0xF;
	//const unsigned location = (pin->pwm_config >> 0) & 0xF;
	return TIMER1->CC[channel].CCV;
}

int mp_hal_pwm_top_get(void)
{
    return pwm_top;
}

void mp_hal_pwm_duty(mp_hal_pwm_obj_t pwm, int duty)
{
	const uint8_t pwm_config = pwm->pin->pwm_config;
	if (pwm_config == 0xFF)
		mp_raise_ValueError("PWM not supported");

    if (duty < 0) {
        duty = 0;
    } else if ((uint32_t)duty > pwm_top) {
        duty = pwm_top;
    }

	const unsigned channel = (pwm_config >> 4) & 0xF;
	const unsigned location = (pwm_config >> 0) & 0xF;

	if (!pwm->active)
	{
		pwm->active = 1;
		pwm_active_channels++;

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
	if (pwm_active_channels > 0)
		pwm_active_channels--;

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
	TIMER1->ROUTEPEN &= ~(0x1UL << channel);

	if (pwm_active_channels == 0) {
		CMU_ClockEnable(cmuClock_TIMER1, false);
		pwm_init_done = 0;
	}
}
