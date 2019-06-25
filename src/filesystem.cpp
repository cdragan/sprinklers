
extern "C" {
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "spi_flash.h"
}

#include "filesystem.h"

static uint32_t flash_size_b = 0;
static uint32_t data_begin   = 0;
static uint32_t data_end     = 0;

static void ICACHE_FLASH_ATTR configure_flash()
{
    switch (system_get_flash_size_map()) {

        case FLASH_SIZE_2M:
            flash_size_b = 256u * 1024u;
            break;

        case FLASH_SIZE_4M_MAP_256_256:
            flash_size_b = 512u * 1024u;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            flash_size_b = 1024u * 1024u;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
            flash_size_b = 2u * 1024u * 1024u;
            data_begin   = 1024u * 1024u;
            break;

        case FLASH_SIZE_16M_MAP_1024_1024:
            flash_size_b = 2u * 1024u * 1024u;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
            flash_size_b = 4u * 1024u * 1024u;
            data_begin   = 1024u * 1024u;
            break;

        case FLASH_SIZE_32M_MAP_1024_1024:
            flash_size_b = 4u * 1024u * 1024u;
            data_begin   = 2u * 1024u * 1024u;
            break;

        case FLASH_SIZE_32M_MAP_2048_2048:
            flash_size_b = 4u * 1024u * 1024u;
            break;

        case FLASH_SIZE_64M_MAP_1024_1024:
            flash_size_b = 8u * 1024u * 1024u;
            data_begin   = 2u * 1024u * 1024u;
            break;

        case FLASH_SIZE_128M_MAP_1024_1024:
            flash_size_b = 16u * 1024u * 1024u;
            data_begin   = 2u * 1024u * 1024u;
            break;

        default:
            break;
    }

    data_end = flash_size_b - 5u * SPI_FLASH_SEC_SIZE; // 5 sectors for user_rf_cal_sector_set()
    if ( ! data_begin)
        data_begin = data_end;
}

// This function returns the index of the sector where the SDK can store its data.
// With SDK 2.1.0, 5 sectors at the end of flash are reserved:
// * 1 sector for RF cal
// * 1 sector for RF init data
// * 3 sectors for SDK parameters
extern "C" uint32_t ICACHE_FLASH_ATTR user_rf_cal_sector_set()
{
    configure_flash();
    return data_end >> 12;
}

static uint32_t ICACHE_FLASH_ATTR calc_checksum(const uint32_t* begin, const uint32_t* end)
{
    uint32_t checksum = 0;
    while (begin < end)
        checksum -= *(begin++);
    return checksum;
}

static filesystem* fs = nullptr;

void ICACHE_FLASH_ATTR init_filesystem()
{
    if (fs)
        return;

    filesystem hdr;
    if (spi_flash_read(data_begin, reinterpret_cast<uint32_t*>(&hdr), sizeof(hdr))
            != SPI_FLASH_RESULT_OK) {
        os_printf("Error: failed to read filesystem header\n");
        return;
    }

    if (hdr.magic != FILESYSTEM_MAGIC) {
        os_printf("Error: filesystem magic is 0x%08x, but should be 0x%08x\n",
                  hdr.magic, FILESYSTEM_MAGIC);
        return;
    }

    constexpr uint32_t max_files = 1 + (SPI_FLASH_SEC_SIZE - sizeof(hdr)) / sizeof(file_entry);

    if (hdr.num_files == 0 || hdr.num_files > max_files) {
        os_printf("Error: incorrect number of files: %u (must be from 1 to %u)\n",
                  hdr.num_files, max_files);
        return;
    }

    const uint32_t fs_size = sizeof(filesystem) + sizeof(file_entry) * (hdr.num_files - 1);

    fs = static_cast<filesystem*>(os_malloc(fs_size));

    if (!fs) {
        os_printf("Error: failed to allocate memory\n");
        return;
    }

    if (spi_flash_read(data_begin, reinterpret_cast<uint32_t*>(fs), fs_size)
            != SPI_FLASH_RESULT_OK) {
        os_printf("Error: failed to read filesystem\n");
        os_free(fs);
        fs = nullptr;
        return;
    }

    const uint32_t* src = &fs->num_files; // skip magic and checksum
    const uint32_t* end = reinterpret_cast<uint32_t*>(fs) + fs_size / sizeof(uint32_t);
    const uint32_t checksum = calc_checksum(src, end);

    if (checksum != hdr.checksum) {
        os_printf("Error: incorrect checksum 0x%08x, but should be 0x%08x\n",
                  checksum, hdr.checksum);
        os_free(fs);
        fs = nullptr;
    }
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

char* ICACHE_FLASH_ATTR load_file(const file_entry* file,
                                  int               size_in_front)
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

    // Pad with zeroes for checksum
    *reinterpret_cast<uint32_t*>(buf[alloc_size - 4]) = 0;

    if (spi_flash_read(offset, reinterpret_cast<uint32_t*>(buf + size_in_front), file->size)
            != SPI_FLASH_RESULT_OK) {
        os_printf("Error: failed to read file\n");
        os_free(buf);
        return nullptr;
    }

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
