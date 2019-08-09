#pragma once

#include <stddef.h>
#include <stdint.h>

struct filesystem;

namespace mock {

    int set_args(int argc, char* argv[]);

    void set_timestamp(uint32_t new_timestamp);

    void run_timers();

    void clear_flash();

    uint8_t modify_filesystem(uint32_t offset, uint8_t value);

    void destroy_filesystem();

    void reboot();

    uint16_t get_flash_lifetime();

    struct file_desc
    {
        const char* filename;
        const char* contents;
    };

    void load_fs_from_memory(const file_desc* files, size_t num_files);

    template<typename T, int N>
    constexpr T align_up(T v)
    {
        return (((v - static_cast<T>(1)) / static_cast<T>(N)) * static_cast<T>(N)) + static_cast<T>(N);
    }

    class fsmaker
    {
        public:
            fsmaker() = default;
            ~fsmaker() { destroy(); }

            fsmaker(const fsmaker&) = delete;
            fsmaker& operator=(const fsmaker&) = delete;

            fsmaker(fsmaker&& other)
                : fs(other.fs), size(other.size)
            {
                other.fs   = nullptr;
                other.size = 0u;
            }

            fsmaker& operator=(fsmaker&& other)
            {
                destroy();
                fs         = other.fs;
                size       = other.size;
                other.fs   = nullptr;
                other.size = 0u;
                return *this;
            }

            void* get_buffer() const { return fs; }
            size_t get_size() const { return size; }

            void construct(const file_desc* files, size_t num_files);

        private:
            void destroy();

            filesystem* fs   = nullptr;
            size_t      size = 0u;
    };

}
