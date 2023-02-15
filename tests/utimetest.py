import os 

user_ = os.environ.get("USER")
filename_ = "/tmp/" + user_ + "/mount/utimetest.txt"
fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened /mount/utimetest successfully with fd", fd)

os.close(fd)
print("File closed successfully...")

# update time
res = os.utime(filename_, (1, 3))
print(res)
print(os.stat(filename_))
print("Terminated successfully.")