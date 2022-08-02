#pragma once

#include "pvp.h"

err_t pmem_init(size_t bytes);
ha_t pmem_ha(gra_t ra);
err_t pmem_gra(ha_t ha, gra_t *gra);
size_t pmem_size();
bool pmem_gra_valid(gra_t ra);
void pmem_to(gra_t dest, void *src, size_t bytes);
void pmem_from(void *dest, gra_t src, size_t bytes);
