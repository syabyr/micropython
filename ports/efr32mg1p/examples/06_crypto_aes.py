from machine import Crypto
from binascii import hexlify, unhexlify
key = unhexlify("000102030405060708090a0b0c0d0e0f")
plain = unhexlify("00112233445566778899aabbccddeeff")
expected = unhexlify("69c4e0d86a7b0430d8cdb78070b4c55a")

out = bytearray(16)
Crypto.aes_ecb_encrypt(key, plain, out)
print("cipher", out.hex())
assert bytes(out) == expected

dec_key = Crypto.aes_decryptkey(key)
back = bytearray(16)
Crypto.aes_ecb_decrypt(dec_key, out, back)
print("plain", back.hex())
assert bytes(back) == plain
print("crypto aes ok")
