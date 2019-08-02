#pragma once

#include <stdint.h>

#ifndef ICACHE_FLASH_ATTR
#define ICACHE_FLASH_ATTR
#endif

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

int os_strncmp(const char* s1, const char* s2, unsigned int n);
void* os_memcpy(void* dest, const void* src, unsigned int n);

}
