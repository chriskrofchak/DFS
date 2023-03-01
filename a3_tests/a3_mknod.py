import os

user_ = os.environ.get("USER")
res = os.mknod("/tmp/" + user_ + "/mount/myfilea3_stat.txt")

print(res)