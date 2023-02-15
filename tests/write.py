import os


user_ = os.environ.get("USER")
fd = os.open("/tmp/" + user_ + "/mount/open_write.txt", os.O_RDWR|os.O_CREAT)
print("Opened /mount/open_write successfully with fd", fd)
res = os.write(fd, bytes("aaaaaaa", 'utf-8'))
print("Successfully wrote:", res, "bytes.")

print(os.stat("/tmp/" + user_ + "/mount/open_write.txt"))

res = os.pread(fd, 7, 0)

print(res)

os.close(fd)

print("Successfully closed file")