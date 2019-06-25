#!/usr/bin/env python

import os
import sys
import struct

if len(sys.argv) != 3:
    print("Usage: mkfs.py <source_dir> <dest_fs_file>")
    sys.exit(1)

dir = sys.argv[1]
if not os.path.isdir(dir):
    print("Error: '" + dir + "' is not a directory");
    sys.exit(1)

def Checksum(data):
    assert (len(data) & 3) == 0
    checksum = 0
    for i in range(0, len(data), 4):
        checksum = (checksum - struct.unpack("<I", data[i : i + 4])[0]) & 0xFFFFFFFF
    return checksum

class Entry:

    def __init__(self, filename):
        path = os.path.join(dir, filename)

        if not os.path.isfile(path):
            print("Error: '" + path + "' is not a file")
            sys.exit(1)

        if len(filename) > 16:
            print("Error: Filename '" + filename + "' is too long (must be max 16 chars)")
            sys.exit(1)

        with open(path, "rb") as f:
            contents = f.read()

        size = len(contents)

        # Pad up to 4 byte boundary with 0s
        if size & 3:
            contents += b'\0' * (4 - (size & 3))

        self.filename = filename
        self.size     = size
        self.checksum = Checksum(contents)
        self.contents = contents

files = list(map(Entry, os.listdir(dir)))

# 28 is size of file_entry structure in bytes, 12 is the header size
fs_hdr_size = len(files) * 28 + 12

data   = bytes()
fs_hdr = struct.pack("<I", len(files))

for file in files:
    offset = len(data) + fs_hdr_size
    data += file.contents

    fs_hdr += struct.pack("<16sIII", file.filename.encode(), file.size, file.checksum, offset)

with open(sys.argv[2], "wb+") as f:
    f.write(struct.pack("<II", 0xC0DEA55A, Checksum(fs_hdr)))
    f.write(fs_hdr)
    f.write(data)
