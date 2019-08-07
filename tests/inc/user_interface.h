#pragma once

#include <stdint.h>

#ifndef ICACHE_FLASH_ATTR
#define ICACHE_FLASH_ATTR
#endif

extern "C" {

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

struct ip_addr {
    uint32_t addr;
};

typedef struct ip_addr ip_addr_t;

struct ip_info {
    struct ip_addr ip;
    struct ip_addr netmask;
    struct ip_addr gw;
};

enum interface_t {
    STATION_IF = 0x123
};

enum op_mode_t {
    STATION_MODE = 0x132
};

enum conn_status_t {
    STATION_GOT_IP = 0x243
};

bool wifi_station_connect();
bool wifi_get_ip_info(interface_t if_index, ip_info* info);
op_mode_t wifi_get_opmode();
bool wifi_set_opmode(op_mode_t opmode);
conn_status_t wifi_station_get_connect_status();

enum wps_type_t {
    WPS_TYPE_DISABLE = 0x71,
    WPS_TYPE_PBC     = 0x85
};

enum wps_cb_status {
    WPS_CB_ST_SUCCESS = 0x61,
    WPS_CB_ST_FAILED  = 0x92,
    WPS_CB_ST_TIMEOUT = 0x21
};

typedef void (*wps_st_cb_t)(int status);

bool wifi_wps_disable();
bool wifi_wps_enable(wps_type_t wps_type);
bool wifi_wps_start();
bool wifi_set_wps_cb(wps_st_cb_t cb);

}
