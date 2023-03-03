import os

user_ = os.environ.get("USER")
filename_ = "/tmp/" + user_ + "/mount/open31.txt"
fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened", filename_, "successfully with fd", fd)

i = 0
while True:
    # i don't want infinite loop... hopefully doesn't take that long
    if (i > 100):
        break
    # try five times, then close filename_
    if (i == 5):
        os.close(fd)

    # increment i
    print(i)
    i += 1

    # try to open file again (should give you error)
    # until at least i == 5
    try:
        fd2 = os.open(filename_, os.O_RDONLY)
    except Exception as e:
        print("Cant open fd2:")
        print(e)
    else:
        break 

print("Successfully opened", filename_, "again with fd2:", fd2)
