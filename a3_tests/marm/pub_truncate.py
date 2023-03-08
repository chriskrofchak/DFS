# Testing operations on already open read-only files.
# Starting student fuse server...
# Starting student fuse client...
# Opening file ('abcd') with O_RDONLY...
# Open OK. Temporarily updating server side file to 'wxyz'...
# Update OK.
# Sleep until cache interval expires...
# Doing getattr, which should overwrite cached file because freshness expired...
# getattr OK. Reading cached file...
# Read OK. truncate (should fail with -EMFILE)...
# Error during test:
# Traceback (most recent call last):
#   File "e2e_already_open_read_only.py", line 125, in <module>
#     raise e
#   File "e2e_already_open_read_only.py", line 97, in <module>
#     assert False, "truncate should have failed with -EMFILE"
# AssertionError: truncate should have failed with -EMFILE

import os
import uuid
import time

make_filename = lambda: str(uuid.uuid4())[:8] + ".txt"

user_ = os.environ.get("USER")
mountpath_ = "/tmp/" + user_ + "/mount/"
filename_ = mountpath_ + make_filename()

fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened", filename_, "successfully with fd", fd)

res = os.write(fd, bytes("wxyzwxyz", "utf-8")) # that should have returned an error if 8 wasn't open...
print("Successfully wrote", res, "bytes")

os.close(fd)
# SETUP...

fd = os.open(filename_, os.O_RDONLY)
res = os.pread(fd, 8, 0)
print("Successfully read:", res)

print("Stat, to check size:")
print(os.stat(filename_))
print("Stat successful")

print("Sleeping for 3s...")
time.sleep(3)

print("Waiting for input to continue:")
while True:
    g = input()
    if g == 'c':
        break

try:
    os.truncate(filename_, 6)
    print("Truncate successful")
except Exception as e:
    print("(Successfully raised error: )")
    print(e)

# # this should send back to server...
# os.utime(fd, (11111222,3333444))
# print("utime successful")

res = os.pread(fd, 8, 0)
print("Read the following successfully:", res)

os.close(fd)
print("Successfully closed file")
