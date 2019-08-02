#pragma once

#include <stddef.h>

extern "C" {

void* os_malloc(size_t size);
void os_free(void* ptr);

}
