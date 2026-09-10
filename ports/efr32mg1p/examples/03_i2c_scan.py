from machine import I2C, Pin

SCL = 0
SDA = 1

i2c = I2C(0, scl=Pin(SCL), sda=Pin(SDA), freq=100000)
addrs = i2c.scan()
print("i2c addresses", [hex(a) for a in addrs])
print("i2c scan ok")
