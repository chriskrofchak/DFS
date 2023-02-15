import os


user_ = os.environ.get("USER")
filename_ = "/tmp/" + user_ + "/mount/trunc.txt"
fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened /mount/trunc successfully with fd", fd)

test_str = "A"*150
test_bytes = bytes(test_str, 'utf-8')
res = os.write(fd, test_bytes)

if (res == 150):
    print("Successfully wrote:", res, "bytes.")
else:
    print("Wrote:", res, "bytes, but this is not", 150)

os.truncate(filename_, 50)

res = os.pread(fd, 50, 0)
if (res == test_bytes):
    print("Successfully read test_string")
else:
    print("Couldnt read all bytes correctly. Read", len(res), "bytes.")
    if (len(res) == 50):
        # print(res[:50])
        # print(res[LEN-150:])
        print(res)
    # print(res)

os.close(fd)
print("Successfully closed file")