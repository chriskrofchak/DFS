import os
import sys
import uuid

user_ = os.environ.get("USER")
mountpath_ = "/tmp/" + user_ + "/mount/"
filename_ = mountpath_ + sys.argv[1] # pass filename to read

print("This trial's filename is:", filename_)
print("Beginning test...\n===")

fd = os.open(filename_, os.O_RDWR)
print("Opened", filename_, "successfully with fd", fd)

res = os.write(fd, bytes("i like everything about you", 'utf-8')) 
# will be less first time if file is empty!
print(res, "bytes written successfully to", filename_)

print("Now staying open in order to hold RDWR.")
while True:
    g = input()
    if g == 'q':
        break

os.close(fd)
print("Closed", filename_, "successfully.")
