
#include "mock_access.h"

#include "../../src/filesystem.h"

#include "espconn.h"
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

namespace {
    class Output {
        public:
            Output() = default;

            void set_output(FILE* f) {
                output = f;
                set = true;
            }

            ~Output() {
                if (set)
                    fclose(output);
            }

            operator FILE*() const {
                return output;
            }

        private:
            FILE* output = stdout;
            bool  set    = false;
    };

    static Output output;
}

int mock::set_args(int argc, char* argv[])
{
    if (argc < 2)
        return 0;

    if (argc > 2) {
        fprintf(stderr, "Error: Invalid parameters\n");
        fprintf(stderr, "Only one parameter is allowed - name of the file for UART output\n");
        return 1;
    }

    FILE* f = fopen(argv[1], "w+");

    if (!f) {
        perror(nullptr);
        return 1;
    }

    output.set_output(f);

    return 0;
}

int os_printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    const int ret = vfprintf(output, format, args);

    va_end(args);

    return ret;
}

int os_sprintf(char* str, const char* format, ...)
{
    va_list args;
    va_start(args, format);

    const int ret = vsprintf(str, format, args);

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

int os_strlen(const char* s)
{
    return strlen(s);
}

int os_strncmp(const char* s1, const char* s2, unsigned int n)
{
    return strncmp(s1, s2, n);
}

void* os_memcpy(void* dest, const void* src, unsigned int n)
{
    return memcpy(dest, src, n);
}

void* os_memmove(void* dest, const void* src, unsigned int n)
{
    return memmove(dest, src, n);
}

int os_memcmp(const void* s1, const void* s2, unsigned int n)
{
    return memcmp(s1, s2, n);
}

int os_strcmp(const char* s1, const char* s2)
{
    return strcmp(s1, s2);
}

char* os_strstr(const char* s1, const char* s2)
{
    return const_cast<char*>(strstr(s1, s2));
}

// Note: values below are hardcoded for NodeMCU 1.0

flash_size_map system_get_flash_size_map()
{
    return FLASH_SIZE_32M_MAP_512_512;
}

static constexpr unsigned num_sectors  = 0x400u;
static constexpr unsigned fs_first_sec = 0x100u;
static constexpr unsigned tail_sectors = 5u; // Used by the SDK
static constexpr uint16_t sec_lifetime = 10000u;

static uint8_t  flash[num_sectors * SPI_FLASH_SEC_SIZE];
static uint8_t  sector_status[num_sectors];
static uint16_t sector_life[num_sectors];

static uint32_t timestamp = 0u;
static int8_t   timezone  = 8;

static wps_st_cb_t   wps_callback  = nullptr;
static espconn       accept_conn;
static bool          accept_called = false;
static mock::buffer* recv_buf      = nullptr;

enum sec_status {
    SEC_ERASED,
    SEC_WRITTEN,
    SEC_BAD
};

SpiFlashOpResult spi_flash_erase_sector(uint16_t sec)
{
    assert(sec >= fs_first_sec);
    assert(sec <  num_sectors - tail_sectors);

    if (sector_life[sec] >= sec_lifetime || sector_status[sec] == SEC_BAD) {
        sector_status[sec] = SEC_BAD;
        return SPI_FLASH_RESULT_ERR;
    }

    ++sector_life[sec];

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
    memset(&sector_life, 0u, sizeof(sector_life));

    timestamp     = 0u;
    timezone      = 8;
    wps_callback  = nullptr;
    accept_called = false;

    user_rf_cal_sector_set();
}

uint8_t mock::modify_filesystem(uint32_t offset, uint8_t value)
{
    assert(offset / SPI_FLASH_SEC_SIZE < num_sectors - tail_sectors - fs_first_sec);

    const uint32_t sec = offset / SPI_FLASH_SEC_SIZE + fs_first_sec;
    assert(sector_status[sec] == SEC_WRITTEN);

    offset += fs_first_sec * SPI_FLASH_SEC_SIZE;

    const uint8_t old_val = flash[offset];
    flash[offset] = value;
    return old_val;
}

void mock::set_timestamp(uint32_t new_timestamp)
{
    timestamp = new_timestamp;
}

void sntp_init()
{
}

void sntp_setservername(unsigned char, char*)
{
}

bool sntp_set_timezone(int8_t new_timezone)
{
    timezone = new_timezone;
    return timezone;
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

bool wifi_station_connect()
{
    return true;
}

bool wifi_get_ip_info(interface_t if_index, ip_info* info)
{
    assert(if_index == STATION_IF);
    assert(info);
    return true;
}

op_mode_t wifi_get_opmode()
{
    return STATION_MODE;
}

bool wifi_set_opmode(op_mode_t opmode)
{
    assert(opmode == STATION_MODE);
    return true;
}

conn_status_t wifi_station_get_connect_status()
{
    return STATION_GOT_IP;
}

bool wifi_wps_disable()
{
    wps_callback = nullptr;
    return true;
}

bool wifi_wps_enable(wps_type_t wps_type)
{
    assert(wps_type == WPS_TYPE_PBC);
    return true;
}

bool wifi_wps_start()
{
    assert(wps_callback);
    wps_callback(WPS_CB_ST_SUCCESS);
    return true;
}

bool wifi_set_wps_cb(wps_st_cb_t cb)
{
    assert(cb);
    assert(!wps_callback);
    wps_callback = cb;
    return true;
}

void espconn_mdns_init(mdns_info* info)
{
    assert(info);
}

int8_t espconn_accept(espconn* conn)
{
    assert(conn);
    assert(!accept_called);
    accept_conn = *conn;
    accept_called = true;
    accept_conn.proto.recv_cb       = nullptr;
    accept_conn.proto.reconnect_cb  = nullptr;
    accept_conn.proto.disconnect_cb = nullptr;
    return 0;
}

int8_t espconn_send(espconn* conn, uint8_t* psent, uint16_t length)
{
    assert(conn);
    assert(psent);
    assert(length);
    assert(recv_buf);

    const size_t pos = recv_buf->size();
    recv_buf->resize(pos + length);
    memcpy(recv_buf->data() + pos, psent, length);
    return 0;
}

int8_t espconn_regist_recvcb(espconn* conn, espconn_recv_callback cb)
{
    assert(conn);
    assert(cb);
    conn->proto.recv_cb = cb;
    return 0;
}

int8_t espconn_regist_reconcb(espconn* conn, espconn_reconnect_callback cb)
{
    assert(conn);
    assert(cb);
    conn->proto.reconnect_cb = cb;
    return 0;
}

int8_t espconn_regist_connectcb(espconn* conn, espconn_connect_callback cb)
{
    assert(conn);
    assert(cb);
    conn->proto.connect_cb = cb;
    return 0;
}

int8_t espconn_regist_disconcb(espconn* conn, espconn_connect_callback cb)
{
    assert(conn);
    assert(cb);
    conn->proto.disconnect_cb = cb;
    return 0;
}

mock::buffer::~buffer()
{
    if (data_)
        free(data_);
}

void mock::buffer::resize(size_t new_size)
{
    if (new_size == 0) {
        if (data_) {
            free(data_);
            data_ = nullptr;
        }
    }
    else if (new_size != size_) {
        if (data_)
            data_ = static_cast<char*>(realloc(data_, new_size));
        else
            data_ = static_cast<char*>(malloc(new_size));
    }
    size_ = new_size;
}

void mock::send_http(const char* request, size_t request_size, buffer* response)
{
    assert(accept_called);
    assert(response);

    espconn conn = accept_conn;

    conn.proto.connect_cb(&conn);

    constexpr size_t block_size = 1400;
    char send_buf[block_size];

    assert(!recv_buf);
    recv_buf = response;

    while (request_size) {
        const size_t send_size = request_size > block_size ? block_size : request_size;

        memcpy(send_buf, request, send_size);

        conn.proto.recv_cb(&conn, send_buf, static_cast<int>(send_size));

        request      += send_size;
        request_size -= send_size;

        // TODO handle 100-continue
        if (response->size())
            break;
    }

    recv_buf = nullptr;

    conn.proto.disconnect_cb(&conn);
}

void mock::send_http(const char* request, buffer* response)
{
    send_http(request, strlen(request), response);
}

void mock::send_http(const buffer& request, buffer* response)
{
    send_http(request.data(), request.size(), response);
}

void mock::reboot()
{
    destroy_filesystem();

    user_rf_cal_sector_set();

    timestamp     = 0u;
    timers        = nullptr;
    wps_callback  = nullptr;
    accept_called = false;
}

uint16_t mock::get_flash_lifetime()
{
    uint16_t max_time = 0u;

    for (unsigned sec = fs_first_sec; sec < num_sectors; ++sec)
        if (sector_life[sec] > max_time)
            max_time = sector_life[sec];

    return max_time;
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
