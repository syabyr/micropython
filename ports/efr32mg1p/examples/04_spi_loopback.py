from machine import SPI, Pin

spi = SPI(0, baudrate=1000000, polarity=0, phase=0, sck=Pin(13), mosi=Pin(15), miso=Pin(14))
tx = b"efr32-spi-loopback"
rx = bytearray(len(tx))
spi.write_readinto(tx, rx)
print("tx", tx)
print("rx", bytes(rx))
print("connect MOSI Pin(15) to MISO Pin(14) for this to pass")
assert bytes(rx) == tx
print("spi loopback ok")
