#pragma once

#include <stdint.h>

extern "C" {

enum SpiFlashOpResult {
    SPI_FLASH_RESULT_OK,
    SPI_FLASH_RESULT_ERR
};

constexpr int SPI_FLASH_SEC_SIZE = 4096;

SpiFlashOpResult spi_flash_erase_sector(uint16_t sec);
SpiFlashOpResult spi_flash_write(uint32_t dst_addr, uint32_t* src_addr, uint32_t size);
SpiFlashOpResult spi_flash_read(uint32_t src_addr, uint32_t* dst_addr, uint32_t size);

}
