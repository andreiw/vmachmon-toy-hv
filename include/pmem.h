#pragma once

#include "pvp.h"

err_t pmem_init(length_t bytes);
ha_t pmem_ha(gra_t ra);
err_t pmem_gra(ha_t ha, gra_t *gra);
length_t pmem_size();
bool pmem_gra_valid(gra_t ra);
length_t pmem_to(gra_t dest, void *src, length_t bytes);
length_t pmem_from(void *dest, gra_t src, length_t bytes);
