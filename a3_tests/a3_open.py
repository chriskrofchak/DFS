import os 

LEN = 50
SIZE = LEN
# SIZE = 140

user_ = os.environ.get("USER")
filename_ = "/tmp/" + user_ + "/mount/open31.txt"
fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened", filename_, "successfully with fd", fd)

os.close(fd)
print("Closed", filename_, "successfully with fd", fd)