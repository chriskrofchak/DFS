# rm -rf /tmp/$USER/cache /tmp/$USER/mount
mkdir -p /tmp/$USER/cache /tmp/$USER/mount

./watdfs_client -s -f -o direct_io /tmp/$USER/cache /tmp/$USER/mount