#pragma once

#include "pvp.h"

err_t pmem_init(length_t bytes);
ha_t pmem_ha(gra_t ra);
err_t pmem_gra(ha_t ha, gra_t *gra);
length_t pmem_size();
bool pmem_gra_valid(gra_t ra);
length_t pmem_to(gra_t dest, const void *src,
                 length_t bytes, length_t access_size);
length_t pmem_from(void *dest, gra_t src, length_t bytes,
                   length_t access_size);

#define PMEM_FROM_NUL_TERM (1 << 0)
#define PMEM_FROM_FORCE_BE (1 << 1)

length_t pmem_from_ex(void *dest, gra_t src,
                      length_t bytes, length_t access_size,
                      uint32_t flags);

#define pmem_from_x(dest, src) ((pmem_from(dest, src, sizeof(*(dest)), sizeof(*(dest))) == sizeof(*(dest))) ? ERR_NONE : ERR_BAD_ACCESS)
