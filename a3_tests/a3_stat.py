import os

user_ = os.environ.get("USER")
filename = "/tmp/" + user_ + "/mount/stat55.txt"
stats = os.stat(filename)

print(stats)
