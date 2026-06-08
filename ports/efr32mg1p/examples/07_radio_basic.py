import radio
import utime

radio.init()
print("mac", radio.mac())
print("pan old", hex(radio.pan()))
print("addr old", hex(radio.address()))

radio.pan(0x1234)
radio.address(0x0001)
radio.channel(11)
radio.promiscuous(True)

print("pan", hex(radio.pan()))
print("addr", hex(radio.address()))
print("promiscuous", radio.promiscuous())

packet = b"\x61\x88\x01\x34\x12\xff\xff\x34\x12\x01\x00hello"
try:
    radio.tx(packet)
    print("tx queued")
except Exception as exc:
    print("tx skipped", exc)

end = utime.ticks_add(utime.ticks_ms(), 3000)
while utime.ticks_diff(end, utime.ticks_ms()) > 0:
    pkt = radio.rx()
    if pkt:
        print("rx", pkt)
    utime.sleep_ms(100)

print("radio basic ok")
