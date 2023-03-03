import os 

user_ = os.environ.get("USER")
filename_ = "/tmp/" + user_ + "/mount/open31.txt"
# this should work! 
fd = os.open(filename_, os.O_RDONLY|os.O_CREAT)
print("Opened", filename_, "successfully with fd", fd)
os.close(fd)
print("Closed", filename_, "successfully with fd", fd)

print("Now trying to open", filename_, "with O_RDWR...")
# now this should fail! 
try:
    fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
except Exception as e:
    print("Got error: ") # we want this! 
    print(e)

print("Terminating successfully.")
# print("Opened", filename_, "successfully with fd", fd)