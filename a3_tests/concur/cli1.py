import os 

user_ = os.environ.get("USER")
filename_ = "/tmp/" + user_ + "/mount/open31.txt"
fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened", filename_, "successfully with fd", fd)

print("now hang until quit...")

while True:
    c = input()
    if (c == 'q'):
        break
    # else keep trying

os.close(fd)
print("Closed", filename_, "successfully with fd", fd)
