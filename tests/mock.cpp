
#include "mock.h"
#include "mock_access.h"

#include "../../src/filesystem.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int os_printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    const int ret = vprintf(format, args);

    va_end(args);

    return ret;
}

void* os_malloc(size_t size)
{
    return malloc(size);
}

void os_free(void* ptr)
{
    free(ptr);
}

int os_strncmp(const char* s1, const char* s2, unsigned int n)
{
    return strncmp(s1, s2, n);
}

void* os_memcpy(void* dest, const void* src, unsigned int n)
{
    return memcpy(dest, src, n);
}

// Note: values below are hardcoded for NodeMCU 1.0

flash_size_map system_get_flash_size_map()
{
    return FLASH_SIZE_32M_MAP_512_512;
}

uint8_t flash[0x400000u];

SpiFlashOpResult spi_flash_erase_sector(uint16_t sec)
{
    assert(sec >= 0x100u);
    assert(sec <  0x3FBu);

    // TODO mock erase failure

    memset(&flash[sec * 0x1000u], 0xFFu, 0x1000u);

    return SPI_FLASH_RESULT_OK;
}

SpiFlashOpResult spi_flash_write(uint32_t dst_addr, uint32_t* src_addr, uint32_t size)
{
    assert(dst_addr >= 0x100000u);
    assert(dst_addr + size <= 0x3FB000u);
    assert(dst_addr % 0x1000u == 0u);

    const auto end = dst_addr + ((size - 1u) & ~0xFFFu) + 0x1000u;
    for (auto i = dst_addr; i < end; ++i)
        assert(flash[i] == 0xFFu);

    // TODO mock write failure

    memcpy(&flash[dst_addr], src_addr, size);

    return SPI_FLASH_RESULT_OK;
}

SpiFlashOpResult spi_flash_read(uint32_t src_addr, uint32_t* dst_addr, uint32_t size)
{
    assert(src_addr >= 0x100000u);
    assert(src_addr + size <= 0x3FB000u);

    // TODO mock read failure

    memcpy(dst_addr, &flash[src_addr], size);

    return SPI_FLASH_RESULT_OK;
}

extern "C" uint32_t user_rf_cal_sector_set();

void mock::clear_flash()
{
    memset(&flash, 0xFFu, sizeof(flash));

    user_rf_cal_sector_set();
}

void mock::load_fs_from_file(const char* filename)
{
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Failed to open file %s\n", filename);
        return;
    }

    fread(&flash[0x100000], 1, 128u * 1024u, file);
    if (ferror(file))
        fprintf(stderr, "Error: Failed to read from file %s\n", filename);

    fclose(file);
}

static uint32_t timestamp = 0u;

void mock::set_timestamp(uint32_t new_timestamp)
{
    timestamp = new_timestamp;
}

uint32_t sntp_get_current_timestamp()
{
    return timestamp;
}

static os_timer_t* timers = nullptr;

void os_timer_arm(os_timer_t* timer, uint32_t time, bool repeat_flag)
{
    assert( ! timer->next);
    assert( ! timer->prev);

    timer->time   = time;
    timer->repeat = repeat_flag;
    timer->next   = timers;
    timers        = timer;
}

void os_timer_disarm(os_timer_t* timer)
{
    if ( ! timer->next && ! timer->prev)
        return;

    if (timer->next)
        timer->next->prev = timer->prev;

    if (timer->prev)
        timer->prev->next = timer->next;
    else
        timers = timer->next;

    timer->next = nullptr;
    timer->prev = nullptr;
}

void os_timer_setfn(os_timer_t* timer, timer_func func, void* arg)
{
    assert( ! timer->next);
    assert( ! timer->prev);

    timer->func = func;
    timer->arg  = arg;
}

void mock::run_timers()
{
    for (auto timer = timers; timer; ) {

        timer->func(timer->arg);

        auto next = timer->next;

        if (!timer->repeat)
            os_timer_disarm(timer);

        timer = next;
    }
}

static uint32_t calc_checksum(const void* buf, size_t size)
{
    const uint32_t* ptr = static_cast<const uint32_t*>(buf);

    uint32_t checksum = 0u;

    while (size > 3u) {
        checksum -= *(ptr++);
        size -= 4u;
    }

    return checksum;
}

namespace mock {

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

            const void* get_buffer() const { return fs; }
            size_t get_size() const { return size; }

            void construct(const file_desc* files, size_t num_files);

        private:
            void destroy();

            filesystem* fs   = nullptr;
            size_t      size = 0u;
    };

}

void mock::fsmaker::construct(const file_desc* files, size_t num_files)
{
    const size_t hdr_size = sizeof(filesystem) + (num_files - 1u) * sizeof(file_entry);
    size = hdr_size;

    for (size_t i = 0; i < num_files; i++)
        size += align_up<size_t, 4>(strlen(files[i].contents));

    fs = static_cast<filesystem*>(malloc(size));

    char* fs_ptr = reinterpret_cast<char*>(fs);

    char* file_buf = fs_ptr + hdr_size;

    fs->magic     = FILESYSTEM_MAGIC;
    fs->num_files = num_files;

    for (size_t i = 0; i < num_files; i++) {
        const size_t file_size = strlen(files[i].contents);
        const size_t aligned = align_up<size_t, 4>(file_size);
        if (file_size > 0) {
            memcpy(file_buf, files[i].contents, file_size);
            if (aligned > file_size)
                memset(file_buf + file_size, 0, aligned - file_size);
        }

        auto& entry = fs->entries[i];

        strncpy(entry.filename, files[i].filename, sizeof(entry.filename));
        entry.size     = file_size;
        entry.checksum = calc_checksum(file_buf, aligned);
        entry.offset   = static_cast<uint32_t>(file_buf - fs_ptr);

        if (aligned > file_size)
            memset(file_buf + file_size, 'x', aligned - file_size);

        file_buf += aligned;
    }

    fs->checksum = calc_checksum(&fs->num_files, hdr_size - 8u);
}

void mock::fsmaker::destroy()
{
    if (fs) {
        free(fs);
        fs   = nullptr;
        size = 0u;
    }
}

void mock::load_fs_from_memory(const file_desc* files, size_t num_files)
{
    fsmaker maker;
    maker.construct(files, num_files);

    const auto fs = maker.get_buffer();
    const auto size = maker.get_size();

    memcpy(&flash[0x100000u], fs, size);
}
