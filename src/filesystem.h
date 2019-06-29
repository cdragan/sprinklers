
#ifndef FILESYSTEM_H_INCLUDED
#define FILESYSTEM_H_INCLUDED

#include "c_types.h"

struct file_entry
{
    char     filename[16];
    uint32_t size;
    uint32_t checksum;
    uint32_t offset; // offset from the beginning of the fs
};

#define FILESYSTEM_MAGIC 0xC0DEA55A

struct filesystem
{
    uint32_t   magic; // must be FILESYSTEM_MAGIC
    uint32_t   checksum;
    uint32_t   num_files;
    file_entry entries[1];
};

int init_filesystem();

const file_entry* find_file(const char* filename);

char* load_file(const file_entry* file, int size_in_front = 0);

int write_fs(const char* data, int size);

#endif
