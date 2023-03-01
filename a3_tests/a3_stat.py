import os

user_ = os.environ.get("USER")
filename = "/tmp/" + user_ + "/mount/myfilea3_stat.txt"
stats = os.stat(filename)

print(stats)
