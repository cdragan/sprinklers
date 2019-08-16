
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

#define FILESYSTEM_MAGIC 0xC0DEA55Au

// The root directory of the filesystem, which is stored at the beginning
// of the first sector of the filesystem.
struct filesystem
{
    uint32_t   magic; // must be FILESYSTEM_MAGIC
    uint32_t   checksum;
    uint32_t   num_files;
    file_entry entries[1];
};

// Initializes the filesystem.
// Must be called before any other function below.
int init_filesystem();

// Searches for a file in the filesystem.
// Returns a pointer to the file_entry structure if the file was found.
// If the file was not found, returns nullptr.
const file_entry* find_file(const char* filename);

// Loads a file from the filesystem.
// - file          - the file to load, returned by find_file
// - size_in_front - number of bytes to allocate in front of file data.
// Allocates a buffer and loads the data from the file
// at offset 'size_in_front'.  The size of the allocated buffer
// is the size of the file plus 'size_in_front'.
// Returns a pointer to the allocated buffer.  The returned
// buffer must be freed by the caller with os_free().
// Upon failure (e.g. when data in flash is corrupted), returns nullptr.
char* load_file(const file_entry* file, int size_in_front = 0);

constexpr uint32_t max_fs_size = 128u * 1024u;

// Write data to the filesystem.
// - offset - offset from the beginning of the file system
// - data   - pointer to bytes to write
// - size   - number of bytes to write
// Note: offset must be a multiple of sector size.
// Returns 0 if the write was completed successfuly or 1 if it failed.
int write_fs(unsigned offset, const char* data, int size);

struct config_base
{
    uint32_t checksum;        // checksum from next field
    uint32_t id;              // id of the write
    uint32_t timestamp;       // timestamp of the write
    uint32_t first_timestamp; // first timestamp ever written
};

// Loads system log/configuration.
// Seeks the log area of the flash for the last log entry and loads it.
// Returns a pointer to the loaded log entry, the size is equal to sector size.
// The returned pointer must not be freed by the caller.
// On subsequent calls, the same pointer will be returned, so if the data was
// modified by the caller, it will remain modified.
// If the log was never written before, id will be set to all Fs
// to indicate that the contents are bogus.
// On failure, e.g. on corruption (bad checksum), returns a nullptr.
config_base* load_config();

// Writes system log/configuration to the flash.
// Increments config_base->id, computes checksum and performs write
// to the next sector in the log area in the flash.
// Returns 0 if the write was successful or 1 if write failed.
int save_config(config_base* config);

#endif
