import os 

LEN = 65535
SIZE = 2*LEN + 500

user_ = os.environ.get("USER")
fd = os.open("/tmp/" + user_ + "/mount/long_write.txt", os.O_RDWR|os.O_CREAT)
print("Opened /mount/long_write successfully with fd", fd)

test_string = 'z'*SIZE
res = os.write(fd, bytes(test_string, 'utf-8'))
if (res == SIZE):
    print("Successfully wrote:", res, "bytes.")
else:
    print("Wrote:", res, "bytes, but this is not", SIZE)

# print(os.stat("/tmp/" + user_ + "/mount/open_write.txt"))

res = os.pread(fd, SIZE, 0)
if (res == test_string):
    print("Successfully read test_string")
else:
    print("Couldnt read all bytes correctly. Read", len(res), "bytes.")
    # print(res)

os.close(fd)
print("Successfully closed file")