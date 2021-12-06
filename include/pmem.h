#pragma once

#include "pvp.h"

err_t pmem_init(size_t bytes);
vm_address_t pmem_base();
size_t pmem_size();
void pmem_to(gra_t dest, void *src, size_t bytes);
void pmem_from(void *dest, gra_t src, size_t bytes);
