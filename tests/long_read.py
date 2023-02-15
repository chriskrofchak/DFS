import os 

LEN = 50
SIZE = LEN
# SIZE = 140

user_ = os.environ.get("USER")
filename_ = "/tmp/" + user_ + "/mount/long_read.txt"
fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened /mount/long_read successfully with fd", fd)

test_string = 'G'*SIZE
res = os.write(fd, bytes(test_string, 'utf-8'))
if (res == SIZE):
    print("Successfully wrote:", res, "bytes.")
else:
    print("Wrote:", res, "bytes, but this is not", SIZE)

# print(os.stat("/tmp/" + user_ + "/mount/open_write.txt"))

res = os.pread(fd, 3*SIZE, 0)
if (res == bytes(test_string, 'utf-8')):
    print("Successfully read test_string")
else:
    print("Couldnt read all bytes correctly. Read", len(res), "bytes.")
    if (len(res) == SIZE):
        # print(res[:50])
        # print(res[LEN-150:])
        print(res)
    # print(res)

os.close(fd)
print("Successfully closed file")