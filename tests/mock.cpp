
#include "mock_access.h"

#include "../../src/filesystem.h"

#include "mem.h"
#include "osapi.h"
#include "sntp.h"
#include "spi_flash.h"
#include "user_interface.h"

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

static constexpr unsigned num_sectors  = 0x400u;
static constexpr unsigned fs_first_sec = 0x100u;
static constexpr unsigned tail_sectors = 5u; // Used by the SDK

static uint8_t flash[num_sectors * SPI_FLASH_SEC_SIZE];
static uint8_t sector_status[num_sectors];

enum sec_status {
    SEC_ERASED,
    SEC_WRITTEN,
    SEC_BAD
};

SpiFlashOpResult spi_flash_erase_sector(uint16_t sec)
{
    assert(sec >= fs_first_sec);
    assert(sec <  num_sectors - tail_sectors);

    if (sector_status[sec] == SEC_BAD)
        return SPI_FLASH_RESULT_ERR;

    memset(&flash[sec * SPI_FLASH_SEC_SIZE], 0xFFu, SPI_FLASH_SEC_SIZE);
    sector_status[sec] = SEC_ERASED;

    return SPI_FLASH_RESULT_OK;
}

SpiFlashOpResult spi_flash_write(uint32_t dst_addr, uint32_t* src_addr, uint32_t size)
{
    assert(dst_addr >= fs_first_sec * SPI_FLASH_SEC_SIZE);
    assert(dst_addr + size <= (num_sectors - tail_sectors) * SPI_FLASH_SEC_SIZE);
    assert(dst_addr % SPI_FLASH_SEC_SIZE == 0u);

    const auto begin_sec = dst_addr / SPI_FLASH_SEC_SIZE;
    const auto end_sec   = ((dst_addr + size - 1u) / SPI_FLASH_SEC_SIZE) + 1u;

    for (auto i = begin_sec; i < end_sec; ++i) {
        if (sector_status[i] != SEC_ERASED)
            return SPI_FLASH_RESULT_ERR;
        sector_status[i] = SEC_WRITTEN;
    }

    memcpy(&flash[dst_addr], src_addr, size);

    return SPI_FLASH_RESULT_OK;
}

SpiFlashOpResult spi_flash_read(uint32_t src_addr, uint32_t* dst_addr, uint32_t size)
{
    assert(src_addr >= fs_first_sec * SPI_FLASH_SEC_SIZE);
    assert(src_addr + size <= (num_sectors - tail_sectors) * SPI_FLASH_SEC_SIZE);

    const auto begin_sec = src_addr / SPI_FLASH_SEC_SIZE;
    const auto end_sec   = ((src_addr + size - 1u) / SPI_FLASH_SEC_SIZE) + 1u;

    for (auto i = begin_sec; i < end_sec; ++i)
        if (sector_status[i] == SEC_BAD)
            return SPI_FLASH_RESULT_ERR;

    memcpy(dst_addr, &flash[src_addr], size);

    return SPI_FLASH_RESULT_OK;
}

extern "C" uint32_t user_rf_cal_sector_set();

void mock::clear_flash()
{
    memset(&flash, 0xFFu, sizeof(flash));
    memset(&sector_status, SEC_ERASED, sizeof(sector_status));

    user_rf_cal_sector_set();
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

    memcpy(&flash[fs_first_sec * SPI_FLASH_SEC_SIZE], fs, size);

    const auto end_sec = ((fs_first_sec * SPI_FLASH_SEC_SIZE + size - 1u) / SPI_FLASH_SEC_SIZE) + 1u;
    memset(&sector_status[fs_first_sec], SEC_WRITTEN, end_sec - fs_first_sec);
}
