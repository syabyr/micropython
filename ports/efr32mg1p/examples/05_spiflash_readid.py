from machine import SPI, SPIFlash, Pin

spi = SPI(0, baudrate=1000000, sck=Pin(13), mosi=Pin(15), miso=Pin(14))
flash = SPIFlash(spi, Pin(12))
jedec = flash.readid()
print("jedec", jedec.hex())
assert len(jedec) == 3
assert jedec != b"\x00\x00\x00"
assert jedec != b"\xff\xff\xff"
print("spiflash readid ok")
