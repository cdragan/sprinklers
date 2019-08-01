
#include "mock.h"

struct filesystem;

namespace mock {

    void set_timestamp(uint32_t new_timestamp);

    void run_timers();

    void clear_flash();

    void load_fs_from_file(const char* filename);

    void destroy_filesystem();

    struct file_desc
    {
        const char* filename;
        const char* contents;
    };

    void load_fs_from_memory(const file_desc* files, size_t num_files);
}
