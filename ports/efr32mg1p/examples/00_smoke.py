import gc
import sys
import utime
from machine import Pin

print("platform", sys.platform)
print("mem_free", gc.mem_free())

p = Pin(0, Pin.OUT)
p.on()
utime.sleep_ms(50)
assert p.value() == 1
p.off()
utime.sleep_ms(50)
assert p.value() == 0
p.toggle()
assert p.value() == 1
p.off()

start = utime.ticks_ms()
utime.sleep_ms(20)
elapsed = utime.ticks_diff(utime.ticks_ms(), start)
print("elapsed_ms", elapsed)
assert elapsed >= 15

print("smoke ok")
