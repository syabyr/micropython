import utime
from machine import Pin

DQ_PIN = 0

CMD_SKIP_ROM = 0xCC
CMD_CONVERT_T = 0x44
CMD_READ_SCRATCHPAD = 0xBE


def _drive_low(pin):
    pin.init(Pin.OUT)
    pin.value(0)


def _release(pin):
    pin.init(Pin.IN, Pin.PULL_UP)


def reset(pin):
    _drive_low(pin)
    utime.sleep_us(480)
    _release(pin)
    utime.sleep_us(70)
    presence = pin.value() == 0
    utime.sleep_us(410)
    return presence


def write_bit(pin, bit):
    _drive_low(pin)
    if bit:
        utime.sleep_us(6)
        _release(pin)
        utime.sleep_us(64)
    else:
        utime.sleep_us(60)
        _release(pin)
        utime.sleep_us(10)


def read_bit(pin):
    _drive_low(pin)
    utime.sleep_us(6)
    _release(pin)
    utime.sleep_us(9)
    bit = pin.value()
    utime.sleep_us(55)
    return bit


def write_byte(pin, value):
    for _ in range(8):
        write_bit(pin, value & 1)
        value >>= 1


def read_byte(pin):
    value = 0
    for i in range(8):
        value |= read_bit(pin) << i
    return value


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8C
            else:
                crc >>= 1
    return crc


def read_temperature(pin):
    if not reset(pin):
        raise RuntimeError("DS18B20 not found")
    write_byte(pin, CMD_SKIP_ROM)
    write_byte(pin, CMD_CONVERT_T)

    for _ in range(1000):
        if read_bit(pin):
            break
        utime.sleep_ms(1)
    else:
        raise RuntimeError("temperature conversion timeout")

    if not reset(pin):
        raise RuntimeError("DS18B20 disappeared")
    write_byte(pin, CMD_SKIP_ROM)
    write_byte(pin, CMD_READ_SCRATCHPAD)
    scratch = bytes(read_byte(pin) for _ in range(9))

    if crc8(scratch) != 0:
        raise RuntimeError("scratchpad crc error: " + scratch.hex())

    raw = scratch[0] | (scratch[1] << 8)
    if raw & 0x8000:
        raw -= 0x10000
    return raw / 16, scratch


pin = Pin(DQ_PIN, Pin.IN, Pin.PULL_UP)
print("DS18B20 DQ on Pin(%d); connect 4.7k pull-up to VCC if internal pull-up is unreliable" % DQ_PIN)

while True:
    temp, scratch = read_temperature(pin)
    print("temperature", temp, "C", "scratch", scratch.hex())
    utime.sleep_ms(1000)
