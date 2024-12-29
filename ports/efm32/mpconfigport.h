#include <stdint.h>

#include <mpconfigboard.h>

// options to control how MicroPython is built

// You can disable the built-in MicroPython compiler by setting the following
// config option to 0.  If you do this then you won't get a REPL prompt, but you
// will still be able to execute pre-compiled scripts, compiled with mpy-cross.
#define MICROPY_ENABLE_COMPILER     (1)

#define MICROPY_QSTR_BYTES_IN_HASH  (1)
#define MICROPY_QSTR_EXTRA_POOL     mp_qstr_frozen_const_pool
#define MICROPY_ENABLE_GC                 (1)
#define MICROPY_HELPER_REPL               (1)
#define FROZEN_DIR (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT    (1)

#define MICROPY_ALLOC_PATH_MAX      (256)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)
#define MICROPY_EMIT_X64            (0)
#define MICROPY_EMIT_THUMB          (1)
#define MICROPY_EMIT_INLINE_THUMB   (0)
#define MICROPY_COMP_MODULE_CONST   (0)
#define MICROPY_COMP_CONST          (0)
#define MICROPY_COMP_DOUBLE_TUPLE_ASSIGN (0)
#define MICROPY_COMP_TRIPLE_TUPLE_ASSIGN (0)
#define MICROPY_PY_MICROPYTHON_MEM_INFO (1)
#define MICROPY_STACK_CHECK         (1)
#define MICROPY_ENABLE_PYSTACK	    (0)
#define MICROPY_MALLOC_USES_ALLOCATED_SIZE (1)
#define MICROPY_DEBUG_PRINTERS      (0)
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_ENABLE_FINALISER    (1)
#define MICROPY_GC_ALLOC_THRESHOLD  (0)
//#define MICROPY_REPL_EVENT_DRIVEN   (0)
#define MICROPY_REPL_EMACS_KEYS     (1)
#define MICROPY_HELPER_REPL         (1)
#define MICROPY_HELPER_LEXER_UNIX   (0)
#define MICROPY_ENABLE_SOURCE_LINE  (1)
#define MICROPY_ENABLE_DOC_STRING   (0)
#define MICROPY_ENABLE_SCHEDULER    (1)
#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_BUILTIN_METHOD_CHECK_SELF_ARG (0)
#define MICROPY_PY_ASYNC_AWAIT      (0)
#define MICROPY_PY_BUILTINS_INPUT   (1)
#define MICROPY_PY_BUILTINS_BYTEARRAY (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (1)
#define MICROPY_PY_BUILTINS_DICT_FROMKEYS (0)
#define MICROPY_PY_BUILTINS_ENUMERATE (0)
#define MICROPY_PY_BUILTINS_FILTER  (0)
#define MICROPY_PY_BUILTINS_FROZENSET (0)
#define MICROPY_PY_BUILTINS_REVERSED (0)
#define MICROPY_PY_BUILTINS_SET     (0)
#define MICROPY_PY_BUILTINS_SLICE   (1)
#define MICROPY_PY_BUILTINS_PROPERTY (0)
#define MICROPY_PY_BUILTINS_MIN_MAX (0)
#define MICROPY_PY_BUILTINS_STR_COUNT (0)
#define MICROPY_PY_BUILTINS_STR_OP_MODULO (1)
#define MICROPY_PY___FILE__         (0)
#define MICROPY_PY_GC               (1)
#define MICROPY_PY_ARRAY            (1)
#define MICROPY_PY_ARRAY_SLICE_ASSIGN   (1)
#define MICROPY_PY_ATTRTUPLE        (0)
#define MICROPY_PY_COLLECTIONS      (0)
#define MICROPY_PY_MATH             (1)
#define MICROPY_PY_CMATH            (0)
#define MICROPY_PY_IO               (1)
#define MICROPY_PY_IO_FILEIO        (1)
#define MICROPY_PY_STRUCT           (1)

#define MICROPY_PY_OS                               (1)
#define MICROPY_PY_OS_INCLUDEFILE                   "ports/efm32/moduos.c"
//#define MICROPY_PY_OS_DUPTERM                       (1)
#define MICROPY_PY_OS_SYNC                          (1)
#define MICROPY_PY_OS_URANDOM                       (1)

#define MICROPY_PY_SYS              (1)
#define MICROPY_PY_SYS_STDFILES     (1)
#define MICROPY_PY_SYS_STDIO_BUFFER (1)
#define MICROPY_PY_SYS_EXIT         (1)
//#define MICROPY_PY_USELECT          (1)
#define MICROPY_PY_MACHINE          (1)
#define MICROPY_PY_UTIME_MP_HAL     (1)
//#define MICROPY_MODULE_FROZEN       (1)
//#define MICROPY_MODULE_FROZEN_MPY   (1)
#define MICROPY_MODULE_WEAK_LINKS   (1)
#define MICROPY_PERSISTENT_CODE_LOAD (1)
#define MICROPY_STREAMS_NON_BLOCK   (1)
#define MICROPY_STREAMS_POSIX_API   (1)
#define MICROPY_CPYTHON_COMPAT      (0)
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_KBD_EXCEPTION       (1)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)

// pwm
#define MICROPY_PY_MACHINE_PWM              (1)
#define MICROPY_PY_MACHINE_PWM_INIT         (1)
#define MICROPY_PY_MACHINE_PWM_DUTY         (1)
#define MICROPY_PY_MACHINE_PWM_DUTY_U16_NS  (1)
#define MICROPY_PY_MACHINE_PWM_INCLUDEFILE  "ports/efm32/pwm.c"

// Allow VFS access to the flash chip
#define MICROPY_PY_MACHINE_SPI      (1)
#define MICROPY_PY_MACHINE_SPIFLASH (1)
#define MICROPY_PY_MACHINE_SOFTSPI          (1)
#define MICROPY_READER_VFS              (MICROPY_VFS)
#define MICROPY_VFS                     (1)
#define MICROPY_VFS_FAT                 (0)
#define MICROPY_VFS_LFS1                (0)
#define MICROPY_VFS_LFS2                (1)

#if MICROPY_VFS_FAT
#define MICROPY_FATFS_ENABLE_LFN       (1)
#define MICROPY_FATFS_RPATH            (2)
//#define MICROPY_FATFS_MIN_SS           (4096)
#define MICROPY_FATFS_MAX_SS           (4096)
#define MICROPY_FATFS_NORTC            (1)
#define MICROPY_FATFS_LFN_CODE_PAGE    437 /* 1=SFN/ANSI 437=LFN/U.S.(OEM) */
#define mp_type_fileio mp_type_vfs_fat_fileio
#define mp_type_textio mp_type_vfs_fat_textio
#elif MICROPY_VFS_LFS1
#define mp_type_fileio mp_type_vfs_lfs1_fileio
#define mp_type_textio mp_type_vfs_lfs1_textio
#elif MICROPY_VFS_LFS2
#define mp_type_fileio mp_type_vfs_lfs2_fileio
#define mp_type_textio mp_type_vfs_lfs2_textio
#endif


// use vfs's functions for import stat and builtin open
//#define mp_import_stat mp_vfs_import_stat
//#define mp_builtin_open mp_vfs_open
#define mp_builtin_open_obj mp_vfs_open_obj


// C version of AES
//#define MICROPY_PY_UCRYPTOLIB           (1)
#define MICROPY_PY_UBINASCII        (1)
#define MICROPY_PY_BINASCII         (1)


// extra built in modules

// used by zbpy.NIC
#define MICROPY_PY_BUILTINS_BYTES_HEX (1)



// type definitions for the specific machine

#define MICROPY_MAKE_POINTER_CALLABLE(p) ((void*)((mp_uint_t)(p) | 1))

// This port is intended to be 32-bit, but unfortunately, int32_t for
// different targets may be defined in different ways - either as int
// or as long. This requires different printf formatting specifiers
// to print such value. So, we avoid int32_t and use int directly.
#define UINT_FMT "%u"
#define INT_FMT "%d"
typedef int mp_int_t; // must be pointer size
typedef unsigned mp_uint_t; // must be pointer size

#include <sys/types.h>
typedef long mp_off_t;
typedef long off_t;

#define MP_PLAT_PRINT_STRN(str, len) mp_hal_stdout_tx_strn_cooked(str, len)

// Map the pin names to their numbers (identity for now)
typedef const struct _mp_hal_pin_t * mp_hal_pin_obj_t;
#define mp_hal_pin_obj_t mp_hal_pin_obj_t

#define mp_hal_get_pin_obj(pin) (pin)
#define mp_hal_pin_name(p) (p)
#define MP_HAL_PIN_FMT "%u"

// PWM objects and maximum timer value
typedef struct _mp_hal_pwm_t * mp_hal_pwm_obj_t;
#define mp_hal_pwm_obj_t mp_hal_pwm_obj_t
#define MP_HAL_PWM_TOP 8192

// extra built in names to add to the global namespace
extern const struct _mp_obj_module_t uos_module;
extern const struct _mp_obj_module_t utime_module;
extern const struct _mp_obj_module_t machine_module;

#define MICROPY_PORT_BUILTINS \
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&mp_builtin_open_obj) }, \

#define MICROPY_PORT_BUILTIN_MODULES \
    { MP_ROM_QSTR(MP_QSTR_uos), MP_ROM_PTR(&uos_module) }, \
    { MP_ROM_QSTR(MP_QSTR_utime), MP_ROM_PTR(&utime_module) }, \
    { MP_ROM_QSTR(MP_QSTR_machine), MP_ROM_PTR(&machine_module) }, \

// We need to provide a declaration/definition of alloca()
#include <alloca.h>

#define MP_STATE_PORT MP_STATE_VM

#define MICROPY_PORT_ROOT_POINTERS \
    const char *readline_hist[8];
