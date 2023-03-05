import os
import uuid
import time

make_filename = lambda: str(uuid.uuid4())[:8] + ".txt"

user_ = os.environ.get("USER")
mountpath_ = "/tmp/" + user_ + "/mount/"
filename_ = mountpath_ + make_filename()

print("This trial's filename is:", filename_)
print("Beginning test...\n===")

fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened", filename_, "successfully with fd", fd)

print("Sleeping until cache expires...")
time.sleep(3)

print("Writing file (it should flush to server)")
res = os.write(fd, bytes("poppy poppy pop poppy poppy love love", 'utf-8'))
print("Successfully wrote:", res, "bytes.")