import os

user_ = os.environ.get("USER")
res = os.mknod("/tmp/" + user_ + "/mount/stat2800.txt")

print(res)