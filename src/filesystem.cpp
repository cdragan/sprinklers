
extern "C" {
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "sntp.h"
#include "spi_flash.h"
}

#include "filesystem.h"

constexpr uint32_t max_data_size      = 128u * 1024u;
constexpr uint32_t max_writes_per_day = 200u;
constexpr uint32_t sec_per_day        = 60u * 60u * 24u;

static uint32_t  flash_size_b = 0u;
static uint32_t  data_begin   = 0u;
static uint32_t  data_end     = 0u;
static uint32_t& log_begin    = data_end;
static uint32_t  log_end      = 0u;
static bool      supports_ota = false;

static filesystem* fs = nullptr;

static config_base* cfg      = nullptr;
static uint32_t     cfg_addr = ~0u;
static config_base  cfg_last = { ~0u, ~0u, 0u, 0u };

static uint32_t   cfg_write_delayed = 0u;
static os_timer_t cfg_delay_timer;

#ifdef UNIT_TEST
namespace mock {
    void destroy_filesystem()
    {
        if (fs) {
            os_free(fs);
            fs = nullptr;
        }

        if (cfg) {
            os_free(cfg);
            cfg = nullptr;
        }

        flash_size_b = 0u;
        data_begin   = 0u;
        data_end     = 0u;
        log_end      = 0u;
        cfg_addr     = 0u;
    }
}
#endif

static void ICACHE_FLASH_ATTR configure_flash()
{
    // Note: On some boards there isn't enough flash to store user data, so instead
    // we use the second partition (user2) for storing data instead of supporting OTA.

    switch (system_get_flash_size_map()) {

        case FLASH_SIZE_4M_MAP_256_256:
            flash_size_b = 512u * 1024u;
            data_begin   = 256u * 1024u;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            flash_size_b = 1024u * 1024u;
            data_begin   = 512u * 1024u;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
            flash_size_b = 2u * 1024u * 1024u;
            data_begin   = 1024u * 1024u;
            supports_ota = true;
            break;

        case FLASH_SIZE_16M_MAP_1024_1024:
            flash_size_b = 2u * 1024u * 1024u;
            data_begin   = 1024u * 1024u;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
            flash_size_b = 4u * 1024u * 1024u;
            data_begin   = 1024u * 1024u;
            supports_ota = true;
            break;

        case FLASH_SIZE_32M_MAP_1024_1024:
            flash_size_b = 4u * 1024u * 1024u;
            data_begin   = 2u * 1024u * 1024u;
            supports_ota = true;
            break;

        case FLASH_SIZE_32M_MAP_2048_2048:
            flash_size_b = 4u * 1024u * 1024u;
            data_begin   = 2u * 1024u * 1024u;
            break;

        case FLASH_SIZE_64M_MAP_1024_1024:
            flash_size_b = 8u * 1024u * 1024u;
            data_begin   = 2u * 1024u * 1024u;
            supports_ota = true;
            break;

        case FLASH_SIZE_128M_MAP_1024_1024:
            flash_size_b = 16u * 1024u * 1024u;
            data_begin   = 2u * 1024u * 1024u;
            supports_ota = true;
            break;

        default:
            break;
    }

    log_end = flash_size_b - 5u * SPI_FLASH_SEC_SIZE; // 5 sectors for user_rf_cal_sector_set()
    if (data_begin) {
        data_end = data_begin + max_data_size;
        if (data_end > log_end)
            data_end = log_end;
    }
    else {
        data_begin = log_end;
        data_end   = log_end;
    }
}

// This function returns the index of the sector where the SDK can store its data.
// With SDK 2.1.0, 5 sectors at the end of flash are reserved:
// * 1 sector for RF cal
// * 1 sector for RF init data
// * 3 sectors for SDK parameters
extern "C" uint32_t ICACHE_FLASH_ATTR user_rf_cal_sector_set()
{
    configure_flash();
    return data_end / SPI_FLASH_SEC_SIZE;
}

static uint32_t ICACHE_FLASH_ATTR calc_checksum(const uint32_t* begin, const uint32_t* end)
{
    uint32_t checksum = 0;
    while (begin < end) {
        const auto value = *(begin++);
        checksum -= value;
    }
    return checksum;
}

int ICACHE_FLASH_ATTR init_filesystem()
{
    if (fs)
        return 0;

    filesystem hdr;
    if (spi_flash_read(data_begin, reinterpret_cast<uint32_t*>(&hdr), sizeof(hdr))
            != SPI_FLASH_RESULT_OK) {
        os_printf("Error: failed to read filesystem header\n");
        return 1;
    }

    if (hdr.magic != FILESYSTEM_MAGIC) {
        os_printf("Error: filesystem magic is 0x%08x, but should be 0x%08x\n",
                  hdr.magic, FILESYSTEM_MAGIC);
        return 1;
    }

    constexpr uint32_t max_files = 1 + (SPI_FLASH_SEC_SIZE - sizeof(hdr)) / sizeof(file_entry);

    if (hdr.num_files == 0 || hdr.num_files > max_files) {
        os_printf("Error: incorrect number of files: %u (must be from 1 to %u)\n",
                  hdr.num_files, max_files);
        return 1;
    }

    const uint32_t fs_size = sizeof(filesystem) + sizeof(file_entry) * (hdr.num_files - 1);

    fs = static_cast<filesystem*>(os_malloc(fs_size));

    if (!fs) {
        os_printf("Error: failed to allocate memory\n");
        return 1;
    }

    if (spi_flash_read(data_begin, reinterpret_cast<uint32_t*>(fs), fs_size)
            != SPI_FLASH_RESULT_OK) {
        os_printf("Error: failed to read filesystem\n");
        os_free(fs);
        fs = nullptr;
        return 1;
    }

    const uint32_t* src = &fs->num_files; // skip magic and checksum
    const uint32_t* end = reinterpret_cast<uint32_t*>(fs) + fs_size / sizeof(uint32_t);
    const uint32_t checksum = calc_checksum(src, end);

    if (checksum != hdr.checksum) {
        os_printf("Error: incorrect checksum 0x%08x, but should be 0x%08x\n",
                  checksum, hdr.checksum);
        os_free(fs);
        fs = nullptr;
        return 1;
    }

    os_printf("initialized filesystem: %u files\n", hdr.num_files);
    return 0;
}

const file_entry* ICACHE_FLASH_ATTR find_file(const char* filename)
{
    if (!fs)
        return nullptr;

    file_entry* file = &fs->entries[0];
    file_entry* const fs_end = file + fs->num_files;

    for ( ; file != fs_end; ++file) {
        if (os_strncmp(filename, file->filename, sizeof(file->filename)) == 0)
            return file;
    }

    return nullptr;
}

char* ICACHE_FLASH_ATTR load_file(const file_entry* file, int size_in_front)
{
    if (!fs)
        return nullptr;

    if (size_in_front & 3)
        return nullptr;

    const uint32_t offset = file->offset + data_begin;

    if ((offset & 3u) || ! file->size || offset > data_end || offset + file->size > data_end) {
        os_printf("Error: invalid file offset 0x%08x or size 0x%08x\n",
                  file->offset, file->size);
        return nullptr;
    }

    const uint32_t alloc_size = size_in_front + ((file->size - 1u) & ~3u) + 4u;

    char* const buf = static_cast<char*>(os_malloc(alloc_size));
    if (!buf) {
        os_printf("Error: failed to allocate memory\n");
        return nullptr;
    }

    if (spi_flash_read(offset, reinterpret_cast<uint32_t*>(buf + size_in_front), file->size)
            != SPI_FLASH_RESULT_OK) {
        os_printf("Error: failed to read file\n");
        os_free(buf);
        return nullptr;
    }

    // Pad with zeroes for checksum
    for (uint32_t i = size_in_front + file->size; i < alloc_size; i++)
        buf[i] = 0;

    const uint32_t checksum = calc_checksum(reinterpret_cast<uint32_t*>(buf + size_in_front),
                                            reinterpret_cast<uint32_t*>(buf + alloc_size));
    if (checksum != file->checksum) {
        os_printf("Error: file checksum 0x%08x mismatch, expected 0x%08x\n",
                  checksum, file->checksum);
        os_free(buf);
        return nullptr;
    }

    return buf;
}

int ICACHE_FLASH_ATTR write_fs(unsigned offset, const char* data, int size)
{
    if (fs && offset == 0) {
        os_free(fs);
        fs = nullptr;
    }

    if (offset % SPI_FLASH_SEC_SIZE) {
        os_printf("Error: invalid offset 0x%08x\n", offset);
        return 1;
    }

    const uint32_t data_size = data_end - data_begin;
    if (offset > data_size || offset + size > data_size) {
        os_printf("Error: data overflows allocated space in flash, offset 0x%08x size 0x%08x\n",
                  offset, size);
        return 1;
    }

    int sector = (data_begin + offset) / SPI_FLASH_SEC_SIZE;
    const int end_sector = sector + ((size - 1) / SPI_FLASH_SEC_SIZE) + 1;

    for ( ; sector < end_sector; ++sector) {
        os_printf("erase @0x%08x\n", sector * SPI_FLASH_SEC_SIZE);
        if (spi_flash_erase_sector(sector) != SPI_FLASH_RESULT_OK) {
            os_printf("Error: failed to erase sector %d\n", sector);
            return 1;
        }
    }

    uint32_t buf[SPI_FLASH_SEC_SIZE / sizeof(uint32_t)];
    uint32_t dest = data_begin + offset;

    while (size) {

        const int copy_size = size > static_cast<int>(sizeof(buf)) ? sizeof(buf) : size;

        if (copy_size < static_cast<int>(sizeof(buf)))
            buf[copy_size / sizeof(uint32_t)] = 0;

        os_memcpy(buf, data, copy_size);

        const auto write_size = ((copy_size - 1u) & ~3u) + 4u;
        os_printf("write @0x%08x size 0x%04x\n", dest, write_size);
        if (spi_flash_write(dest, buf, write_size) != SPI_FLASH_RESULT_OK) {
            os_printf("Error: failed to write 0x%x bytes at offset 0x%x\n",
                      copy_size, dest);
            return 1;
        }

        data += copy_size;
        size -= copy_size;
        dest += write_size;
    }

    return init_filesystem();
}

static uint32_t ICACHE_FLASH_ATTR calc_config_checksum(config_base* config)
{
    return calc_checksum(&config->checksum + 1, &config->checksum + (SPI_FLASH_SEC_SIZE / 4u));
}

static bool ICACHE_FLASH_ATTR read_config(uint32_t addr, config_base* config)
{
    if (spi_flash_read(addr, &config->checksum, SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK) {
        os_printf("Error: failed to read log from 0x%08x\n", addr);
        return false;
    }

    if (config->id == ~0u && config->checksum == ~0u)
        return true;

    const uint32_t checksum = calc_config_checksum(config);

    if (checksum != config->checksum) {
        os_printf("Error: invalid log entry checksum 0x%08x, expected 0x%08x\n",
                  config->checksum, checksum);
        return false;
    }

    return true;
}

config_base* ICACHE_FLASH_ATTR load_config()
{
    if (log_begin >= log_end) {
        os_printf("Error: config area not available\n");
        return nullptr;
    }

    if (cfg)
        return cfg;

    config_base* low  = static_cast<config_base*>(os_malloc(SPI_FLASH_SEC_SIZE));
    config_base* mid  = static_cast<config_base*>(os_malloc(SPI_FLASH_SEC_SIZE));
    config_base* high = static_cast<config_base*>(os_malloc(SPI_FLASH_SEC_SIZE));

    class free_ptr {
        public:
            explicit free_ptr(config_base*& ptr) ICACHE_FLASH_ATTR : ptr_(ptr) { }
            ~free_ptr() ICACHE_FLASH_ATTR {
                if (ptr_)
                    os_free(ptr_);
            }
        private:
            config_base*& ptr_;
    };
    free_ptr free_low(low);
    free_ptr free_mid(mid);
    free_ptr free_high(high);

    if ( ! low || ! mid || ! high) {
        os_printf("Error: failed to allocate memory\n");
        return nullptr;
    }

    uint32_t low_addr  = log_begin;
    uint32_t high_addr = log_end - SPI_FLASH_SEC_SIZE;

    if (!read_config(low_addr, low))
        return nullptr;

    if (low->id == ~0u)
        low_addr = log_end; // First entry will be saved at log_begin

    else {

        if (!read_config(high_addr, high))
            return nullptr;

        while (low_addr < high_addr) {

            config_base* tmp;

            if (high->id != ~0u && high->id > low->id) {
                tmp      = high;
                high     = low;
                low      = tmp;
                low_addr = high_addr;
                break;
            }

            if (low_addr + SPI_FLASH_SEC_SIZE == high_addr)
                break;

            const uint32_t mid_addr = ((low_addr + high_addr) / (2u * SPI_FLASH_SEC_SIZE))
                                      * SPI_FLASH_SEC_SIZE;

            if (!read_config(mid_addr, mid))
                return nullptr;

            if (mid->id == ~0u || mid->id < low->id) {
                tmp       = high;
                high      = mid;
                mid       = tmp;
                high_addr = mid_addr;
            }
            else {
                tmp       = low;
                low       = mid;
                mid       = tmp;
                low_addr  = mid_addr;
            }
        }

        cfg_last = *low;
    }

    cfg      = low;
    cfg_addr = low_addr;
    low      = nullptr;
    return cfg;
}

bool ICACHE_FLASH_ATTR writing_too_fast(uint32_t timestamp, uint32_t first_timestamp, uint32_t id)
{
    if (timestamp == first_timestamp && ! id)
        return false;

    if (timestamp <= first_timestamp || id == ~0u)
        return true;

    const uint32_t total_lifetime = timestamp - first_timestamp;

    const uint32_t cur_writes_per_day = static_cast<uint32_t>(
            (static_cast<uint64_t>(id) * sec_per_day) / total_lifetime);

    return cur_writes_per_day > max_writes_per_day;
}

static int ICACHE_FLASH_ATTR write_config_sector(config_base* config)
{
    uint32_t write_addr = cfg_addr + SPI_FLASH_SEC_SIZE;

    if (write_addr >= log_end)
        write_addr = log_begin;

    if (write_addr % SPI_FLASH_SEC_SIZE) {
        os_printf("Error: invalid config addr 0x%08x\n", write_addr);
        return 1;
    }

    os_printf("erase @0x%08x\n", write_addr);
    const uint32_t sector = write_addr / SPI_FLASH_SEC_SIZE;
    if (spi_flash_erase_sector(sector) != SPI_FLASH_RESULT_OK) {
        os_printf("Error: failed to erase sector %d\n", sector);
        return 1;
    }

    os_printf("write @0x%08x size 0x%04x\n", write_addr, SPI_FLASH_SEC_SIZE);
    if (spi_flash_write(write_addr, &config->checksum, SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK) {
        os_printf("Error: failed to write 0x%x bytes at offset 0x%x\n",
                  SPI_FLASH_SEC_SIZE, write_addr);
        return 1;
    }

    cfg_addr = write_addr;
    cfg_last = *config;

    return 0;
}

int ICACHE_FLASH_ATTR save_config(config_base* config)
{
    if (!cfg) {
        os_printf("Error: config not initialized\n");
        return 1;
    }

    const uint32_t timestamp = sntp_get_current_timestamp();

    if ( ! timestamp && ! cfg_last.timestamp) {
        os_printf("Error: unable to get time from NTP\n");
        return 1;
    }

    config->first_timestamp =
        cfg_last.first_timestamp ? cfg_last.first_timestamp :
        timestamp                ? timestamp
                                 : 0u;

    // We don't care about overflow here, because long before we reach overflow,
    // the flash will exceed its write limit and will become unusable.  With
    // a 4MB flash size (as in NodeMCU), we use 731 sectors for log area, which
    // gives us roughly 7.3 million entries/writes.  With 200 writes per day
    // the flash could last for 100 years.
    config->id = cfg_last.id + 1u;

    config->timestamp = timestamp;
    config->checksum  = calc_config_checksum(config);

    if (timestamp && writing_too_fast(timestamp, config->first_timestamp, config->id)) {
        if ( ! cfg_write_delayed) {

            const uint32_t today = timestamp / sec_per_day;
            cfg_write_delayed = (today + 1u) * sec_per_day;

            os_timer_disarm(&cfg_delay_timer);
            os_timer_setfn(&cfg_delay_timer, [](void*) ICACHE_FLASH_ATTR {

                const uint32_t timestamp = sntp_get_current_timestamp();

                if (timestamp && timestamp >= cfg_write_delayed) {
                    os_timer_disarm(&cfg_delay_timer);
                    cfg_write_delayed = 0u;

                    if ( ! cfg)
                        return;

                    cfg->id = cfg_last.id + 1u;

                    cfg->first_timestamp = cfg_last.first_timestamp;

                    cfg->timestamp = timestamp;
                    cfg->checksum  = calc_config_checksum(cfg);

                    write_config_sector(cfg);
                }
            }, nullptr);
            os_timer_arm(&cfg_delay_timer, 60000u, true);
        }
        return 0;
    }

    return write_config_sector(config);
}
