#pragma once

#include <stdint.h>

extern "C" {

#define MDNS_NAME "abc" // User configuration

struct mdns_info {
    char*         host_name;
    char*         server_name;
    uint16_t      server_port;
    unsigned long ipAddr;
    char*         txt_data[10];
};

struct esp_tcp {
    int remote_port;
    int local_port;
    uint8_t local_ip[4];
    uint8_t remote_ip[4];
};

enum espconn_type {
    ESPCONN_TCP = 0x444
};

enum espconn_state {
    ESPCONN_NONE = 0x626
};

struct espconn {
    espconn_type type;
    espconn_state state;
    struct {
        esp_tcp* tcp;
    } proto;
};

typedef void (*espconn_connect_callback)(void* arg);
typedef void (*espconn_reconnect_callback)(void* arg, int8_t err);
typedef void (*espconn_recv_callback)(void* arg, char* pdata, unsigned short len);

void espconn_mdns_init(mdns_info* info);
int8_t espconn_accept(espconn* espconn);
int8_t espconn_send(espconn* conn, uint8_t* psent, uint16_t length);
int8_t espconn_regist_recvcb(espconn* conn, espconn_recv_callback cb);
int8_t espconn_regist_reconcb(espconn* conn, espconn_reconnect_callback cb);
int8_t espconn_regist_connectcb(espconn* conn, espconn_connect_callback cb);
int8_t espconn_regist_disconcb(espconn* conn, espconn_connect_callback cb);

}
