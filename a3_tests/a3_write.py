import os

user_ = os.environ.get("USER")
filename_ = "/tmp/" + user_ + "/mount/a3_opn_write.txt"
fd = os.open(filename_, os.O_RDWR|os.O_CREAT)
print("Opened", filename_, "successfully with fd", fd)

res = os.write(fd, bytes("abababa", 'utf-8'))
print("Successfully wrote:", res, "bytes.")

print("Stat, to check size:")
print(os.stat(filename_))

res = os.pread(fd, 7, 0)
print(res)

os.close(fd)
print("Successfully closed file")