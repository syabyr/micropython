import utime
from machine import Pin

count = 0

def on_irq(pin):
    global count
    count += 1
    print("irq", count, pin, pin.value())

p = Pin(0, Pin.IN, Pin.PULL_UP)
p.irq(handler=on_irq, trigger=Pin.IRQ_FALLING | Pin.IRQ_RISING)

print("toggle or short Pin(0) to GND / VCC for 10 seconds")
start = utime.ticks_ms()
while utime.ticks_diff(utime.ticks_ms(), start) < 10000:
    utime.sleep_ms(100)

p.irq(handler=None)
print("irq_count", count)
