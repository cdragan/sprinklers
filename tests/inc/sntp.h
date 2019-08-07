#pragma once

#include <stdint.h>

extern "C" {

void sntp_init();
void sntp_setservername(unsigned char idx, char* server);
bool sntp_set_timezone(int8_t timezone);
uint32_t sntp_get_current_timestamp();

}
