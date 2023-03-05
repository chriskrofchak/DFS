import os
import sys
import uuid

make_filename = lambda: str(uuid.uuid4())[:8] + ".txt"

user_ = os.environ.get("USER")
mountpath_ = "/tmp/" + user_ + "/mount/"
filename_ = mountpath_ + sys.argv[1] # pass filename to read

print("This trial's filename is:", filename_)
print("Beginning test...\n===")

stats = os.stat(filename_)
len = stats.st_size
print("Stat collected correctly")

fd = os.open(filename_, os.O_RDONLY)
print("Opened", filename_, "successfully with fd", fd)

res = os.pread(fd, len, 0) # will be less first time if file is empty!
print(res)

os.close(fd)
print("Closed", filename_, "successfully.")
