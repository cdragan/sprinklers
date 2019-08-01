
#pragma once

#include <stddef.h>
#include <stdint.h>

#define ICACHE_FLASH_ATTR

extern "C" {

typedef void (*timer_func)(void* timer_arg);

struct os_timer_t {
    os_timer_t* prev;
    os_timer_t* next;
    timer_func  func;
    void*       arg;
    uint32_t    time;
    bool        repeat;
};

void os_timer_arm(os_timer_t* timer, uint32_t time, bool repeat_flag);
void os_timer_disarm(os_timer_t* timer);
void os_timer_setfn(os_timer_t* timer, timer_func func, void* arg);

int os_printf(const char* format, ...);
void* os_malloc(size_t size);
void os_free(void* ptr);
int os_strncmp(const char* s1, const char* s2, unsigned int n);
void* os_memcpy(void* dest, const void* src, unsigned int n);

enum flash_size_map {
    FLASH_SIZE_4M_MAP_256_256,
    FLASH_SIZE_2M,
    FLASH_SIZE_8M_MAP_512_512,
    FLASH_SIZE_16M_MAP_512_512,
    FLASH_SIZE_32M_MAP_512_512,
    FLASH_SIZE_16M_MAP_1024_1024,
    FLASH_SIZE_32M_MAP_1024_1024,
    FLASH_SIZE_32M_MAP_2048_2048,
    FLASH_SIZE_64M_MAP_1024_1024,
    FLASH_SIZE_128M_MAP_1024_1024
};

flash_size_map system_get_flash_size_map();

enum SpiFlashOpResult {
    SPI_FLASH_RESULT_OK,
    SPI_FLASH_RESULT_ERR
};

constexpr int SPI_FLASH_SEC_SIZE = 4096;

SpiFlashOpResult spi_flash_erase_sector(uint16_t sec);
SpiFlashOpResult spi_flash_write(uint32_t dst_addr, uint32_t* src_addr, uint32_t size);
SpiFlashOpResult spi_flash_read(uint32_t src_addr, uint32_t* dst_addr, uint32_t size);

uint32_t sntp_get_current_timestamp();

}
