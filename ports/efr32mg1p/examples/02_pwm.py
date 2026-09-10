import utime
from machine import PWM, Pin

pwm = PWM(Pin(0), freq=1000, duty_u16=0)
print("freq", pwm.freq())

for duty in (0, 16384, 32768, 49152, 65535):
    pwm.duty_u16(duty)
    print("duty_u16", pwm.duty_u16())
    utime.sleep_ms(500)

pwm.freq(2000)
print("freq", pwm.freq())
pwm.duty_u16(32768)
utime.sleep_ms(1000)

pwm.deinit()
print("pwm ok")
