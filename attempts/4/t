#!/bin/bash

# TODO: Generate FAT-{12,16,32}
# TODO: Populate generated image with randomly generated text files and directories.

set -eux 
SIZE=${1:-33}
BASE="${SIZE}mb"
IMAGE="${BASE}.img"
# TODO: Change based on image size, and FAT type
SECTOR=2048
rm -fv "${BASE}.img"
rm -fv "${BASE}.hex"
fallocate -l ${SIZE}M "${IMAGE}"
fdisk "${IMAGE}" << EOF
n
p
1
${SECTOR}

t
c
w
EOF

l=$(sudo losetup -f)
echo $l

sudo losetup $l -o 0 "${IMAGE}"
sudo mkfs.vfat -F 32 $l -n TESTING
sleep 1
sudo losetup -d $l
sleep 1
xxd -g 1 "${IMAGE}" > "${BASE}.hex"
