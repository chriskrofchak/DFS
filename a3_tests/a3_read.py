import os
import os 

filename_ = "/tmp/cikrofch/mount/dede7a6d.txt"
fd = os.open(filename_, os.O_RDONLY)

res = os.pread(fd, 7, 0) # should be ababa
print(res)