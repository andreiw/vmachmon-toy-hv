#pragma once

#include "types.h"
#include "list.h"

typedef struct list_head ranges_t;

typedef struct range_s {
  struct list_head link;
  uint32_t base;
  uint32_t limit;
} range_t;

static inline
void range_init(ranges_t *ranges)
{
  INIT_LIST_HEAD(ranges);
}

range_t *range_alloc(uint32_t base, uint32_t limit);
void range_dump(ranges_t *ranges);
void range_add(ranges_t *ranges,
               uint32_t base,
               uint32_t limit);
void range_remove(ranges_t *ranges,
                  uint32_t base,
                  uint32_t limit);
length_t range_count(ranges_t *ranges);
