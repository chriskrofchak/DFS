import os

user_ = os.environ.get("USER")
stats = os.stat("/tmp/" + user_ + "/mount/open_write.txt")
print(stats)



