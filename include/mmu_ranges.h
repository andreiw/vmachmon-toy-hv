#pragma once

#include "types.h"
#include "list.h"

typedef struct list_head mmu_ranges_t;

typedef struct mmu_range_s {
  struct list_head link;
  gea_t base;
  gea_t limit;
  gra_t ra;
  uint32_t flags;
} mmu_range_t;

static inline
void mmu_range_init(mmu_ranges_t *ranges)
{
  INIT_LIST_HEAD(ranges);
}

mmu_range_t *mmu_range_alloc(gea_t base, gea_t limit,
                             gra_t ra, uint32_t flags);
void mmu_range_dump(mmu_ranges_t *ranges);
void mmu_range_add(mmu_ranges_t *ranges,
                   gea_t base, gea_t limit,
                   gra_t ra, uint32_t flags);
mmu_range_t *mmu_range_find(mmu_ranges_t *ranges,
                            gea_t ea);
