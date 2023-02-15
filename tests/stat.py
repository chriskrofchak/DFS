import os

user_ = os.environ.get("USER")
stats = os.stat("/tmp/" + user_ + "/myfile.txt")
print(stats)



