"""
Input pin test script for the efr32mg1p MicroPython port.

Usage (on device REPL):
    import test_pin_input
    test_pin_input.run(pin_id=10, sample_ms=20, report_every_ms=1000)

Pin numbering uses this port's Pin ID table (see ports/efr32mg1p/pin_defs.h).
"""

from machine import Pin
import utime


def run(pin_id=10, sample_ms=20, report_every_ms=1000, pull="up"):
    # Configure selected pin as digital input.
    if pull == "up":
        pin = Pin(pin_id, Pin.IN, Pin.PULL_UP)
    elif pull == "down":
        pin = Pin(pin_id, Pin.IN, Pin.PULL_DOWN)
    elif pull is None:
        pin = Pin(pin_id, Pin.IN)
    else:
        raise ValueError("pull must be 'up', 'down' or None")

    print("[pin-input-test] start")
    print("pin_id=", pin_id, "sample_ms=", sample_ms, "report_every_ms=", report_every_ms, "pull=", pull)
    print("Toggle the pin between GND and VCC, press Ctrl+C to stop.")

    last = pin.value()
    changes = 0
    high_count = 0
    low_count = 0
    t_last_report = utime.ticks_ms()

    print("initial value:", last)

    while True:
        v = pin.value()
        if v:
            high_count += 1
        else:
            low_count += 1

        if v != last:
            changes += 1
            print("change #{} -> {} at {} ms".format(changes, v, utime.ticks_ms()))
            last = v

        now = utime.ticks_ms()
        if utime.ticks_diff(now, t_last_report) >= report_every_ms:
            total = high_count + low_count
            if total == 0:
                high_pct = 0
                low_pct = 0
            else:
                high_pct = (100 * high_count) // total
                low_pct = 100 - high_pct

            print(
                "report: now={} ms value={} changes={} high={} low={} high%={} low%={}".format(
                    now,
                    last,
                    changes,
                    high_count,
                    low_count,
                    high_pct,
                    low_pct,
                )
            )
            t_last_report = now

        utime.sleep_ms(sample_ms)
