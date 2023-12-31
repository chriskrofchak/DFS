import os 
import uuid 

LEN = 50
SIZE = LEN
# SIZE = 140

make_filename = lambda: str(uuid.uuid4())[:8] + ".txt"

user_ = os.environ.get("USER")
mountpath_ = "/tmp/" + user_ + "/mount/"
filename_ = mountpath_ + make_filename()

print("This trial's filename is:", filename_)
print("Beginning test...\n===")

fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened", filename_, "successfully with fd", fd)

os.close(fd)
print("Closed", filename_, "successfully with fd", fd)