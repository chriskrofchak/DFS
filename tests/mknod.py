import os

user_ = os.environ.get("USER")
res = os.mknod("/tmp/" + user_ + "/myfile.txt")

print(res)