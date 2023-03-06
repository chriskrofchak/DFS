# Opening file ('abcd') with O_RDWR...
# Open OK. Temporarily updating server side file to 'wxyz'...
# Update OK.
# Sleep until cache interval expires...
# Doing getattr, which should NOT replace cache file...
# getattr OK. Reading cached file...
# Read OK. Reading server file...
# Read OK. truncate (should update server side file)...
# truncate OK. Sleep until cache interval expires...
# Sleep OK. utimens (should update server side file)...
# Error during test:

import os
import uuid
import time

make_filename = lambda: str(uuid.uuid4())[:8] + ".txt"

user_ = os.environ.get("USER")
mountpath_ = "/tmp/" + user_ + "/mount/"
filename_ = mountpath_ + make_filename()
fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened", filename_, "successfully with fd", fd)

res = os.write(fd, bytes("wxyzwxyz", 'utf-8'))
print("Successfully wrote:", res, "bytes.")

print("Stat, to check size:")
print(os.stat(filename_))
print("Stat successful")

os.truncate(fd, 6)
print("Truncate successful")

print("Sleeping for 3s...")
time.sleep(3)

# this should send back to server...
os.utime(fd, (11111222,3333444))
print("utime successful")

os.close(fd)
print("Successfully closed file")