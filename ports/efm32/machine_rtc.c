//rtc for tradfri
//rtc for efm32

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "py/runtime.h"
#include "shared/timeutils/timeutils.h"
#include "em_rtcc.h"
#include "em_cmu.h"
#include "modmachine.h"

#define DELAY_SECONDS 5.0
#define ULFRCOFREQ    1000
#define COMPARE_TOP   (DELAY_SECONDS * ULFRCOFREQ - 1)

typedef struct _pyb_rtc_obj_t {
    mp_obj_base_t base;
} pyb_rtc_obj_t;

// singleton RTC object
static const pyb_rtc_obj_t pyb_rtc_obj = {{&pyb_rtc_type}};

static mp_obj_t pyb_rtc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    // return constant object
    return (mp_obj_t)&pyb_rtc_obj;
}

static mp_obj_t machine_rtc_datetime_helper(mp_uint_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        // Get time
        return mp_const_none;
    }
    return mp_const_none;
}

void mp_hal_rtc_init(void) {
    // system_get_rtc_time() is always 0 after reset/deepsleep
    //rtc_last_ticks = system_get_rtc_time();

    // reset ALARM0 state
    //pyb_rtc_alarm0_wake = 0;
    //pyb_rtc_alarm0_expiry = 0;

    // Configure the RTCC settings
    RTCC_Init_TypeDef rtcc = RTCC_INIT_DEFAULT;
    rtcc.enable = false;
    rtcc.presc = rtccCntPresc_1;
    rtcc.cntMode = rtccCntModeCalendar;
    rtcc.cntWrapOnCCV1 = true;

    // Configure the compare settings
    RTCC_CCChConf_TypeDef compare = RTCC_CH_INIT_COMPARE_DEFAULT;

    // Turn on the clock for the RTCC
    CMU_ClockEnable(cmuClock_RTCC, true);

    // Initialise RTCC with pre-defined settings
    RTCC_Init(&rtcc);

    // Set current date and time
    RTCC_DateSet(0);
    RTCC_TimeSet(0);

    // Initialise RTCC compare with a date, the date when interrupt will occur
    RTCC_ChannelInit(1, &compare);
    RTCC_ChannelDateSet(1, 0);
    RTCC_ChannelTimeSet(1,0);

    // Set channel 1 to cause an interrupt
    RTCC_IntEnable(RTCC_IEN_CC1);
    NVIC_ClearPendingIRQ(RTCC_IRQn);
    NVIC_EnableIRQ(RTCC_IRQn);

    // Start counter after all initialisations are complete
    RTCC_Enable(true);
}

void rtc_prepare_deepsleep(uint64_t sleep_us) {
    // RTC time will reset at wake up. Let's be preared for this.
    //int64_t delta = pyb_rtc_get_us_since_epoch() + sleep_us;
    //system_rtc_mem_write(MEM_DELTA_ADDR, &delta, sizeof(delta));
}

// for tradfri
static mp_obj_t pyb_rtc_datetime(size_t n_args, const mp_obj_t *args) {
    return machine_rtc_datetime_helper(n_args, args);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_rtc_datetime_obj, 1, 2, pyb_rtc_datetime);


static mp_obj_t pyb_rtc_memory(size_t n_args, const mp_obj_t *args) {
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_rtc_memory_obj, 1, 2, pyb_rtc_memory);

static mp_obj_t pyb_rtc_alarm(mp_obj_t self_in, mp_obj_t alarm_id, mp_obj_t time_in) {
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(pyb_rtc_alarm_obj, pyb_rtc_alarm);

static mp_obj_t pyb_rtc_alarm_left(size_t n_args, const mp_obj_t *args)
{
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_rtc_alarm_left_obj, 1, 2, pyb_rtc_alarm_left);

static mp_obj_t pyb_rtc_irq(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(pyb_rtc_irq_obj, 1, pyb_rtc_irq);


void pyb_rtc_set_us_since_epoch(uint64_t nowus) {
    // set the RTC time
    
}

static const mp_rom_map_elem_t pyb_rtc_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_datetime), MP_ROM_PTR(&pyb_rtc_datetime_obj) },
    { MP_ROM_QSTR(MP_QSTR_memory), MP_ROM_PTR(&pyb_rtc_memory_obj) },
    { MP_ROM_QSTR(MP_QSTR_alarm), MP_ROM_PTR(&pyb_rtc_alarm_obj) },
    { MP_ROM_QSTR(MP_QSTR_alarm_left), MP_ROM_PTR(&pyb_rtc_alarm_left_obj) },
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&pyb_rtc_irq_obj) },
    { MP_ROM_QSTR(MP_QSTR_ALARM0), MP_ROM_INT(0) },
};
static MP_DEFINE_CONST_DICT(pyb_rtc_locals_dict, pyb_rtc_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    pyb_rtc_type,
    MP_QSTR_RTC,
    MP_TYPE_FLAG_NONE,
    make_new, pyb_rtc_make_new,
    locals_dict, &pyb_rtc_locals_dict
    );