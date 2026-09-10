import os
import uos
import binascii

print("=== VERIFY START ===")

# 1) module import sanity
print("IMPORT_OK")

# 2) file read/write/delete sanity
fn = "/__fs_test__.txt"
data = b"LFS2_SMOKE_1234"
with open(fn, "wb") as f:
    f.write(data)

with open(fn, "rb") as f:
    read_back = f.read()

assert read_back == data, "RW_FAIL"
print("RW_OK", len(read_back))

root_list = os.listdir("/")
assert "__fs_test__.txt" in root_list, "LIST_FAIL"
print("LIST_OK")

os.remove(fn)
assert "__fs_test__.txt" not in os.listdir("/"), "CLEAN_FAIL"
print("CLEAN_OK")

# 3) directory create/list/remove sanity
dir_name = "/__fs_dir__"
file_in_dir = dir_name + "/a.txt"

try:
    os.mkdir(dir_name)
except OSError:
    # Reuse existing dir if left by prior interrupted run.
    pass

with open(file_in_dir, "w") as f:
    f.write("ok")

assert "a.txt" in os.listdir(dir_name), "DIR_RW_FAIL"
print("DIR_RW_OK")

os.remove(file_in_dir)
os.rmdir(dir_name)
assert "__fs_dir__" not in os.listdir("/"), "DIR_CLEAN_FAIL"
print("DIR_CLEAN_OK")

# 4) filesystem stats sanity
sv = os.statvfs("/")
assert sv[0] > 0, "STATVFS_BSIZE_FAIL"
assert sv[2] >= 0, "STATVFS_BLOCKS_FAIL"
print("STATVFS_OK", sv[0], sv[2], sv[3])

# 5) binascii hexlify sanity
assert binascii.hexlify(b"ab") == b"6162", "HEXLIFY_FAIL"
print("HEXLIFY_OK")

print("=== VERIFY PASS ===")
