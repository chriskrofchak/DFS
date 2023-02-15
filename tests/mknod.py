import os

user_ = os.environ.get("USER")
res = os.mknod("/tmp/" + user_ + "/mount/myfile3.txt")

print(res)