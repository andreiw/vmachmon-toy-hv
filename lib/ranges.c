#include "pvp.h"
#include "ranges.h"
#include "mon.h"

length_t
range_count(ranges_t *ranges)
{
  range_t *range;
  length_t c = 0;

  list_for_each_entry(range, ranges, link) {
    c++;
  }

  return c;
}

range_t *
range_alloc(uint32_t base,
            uint32_t limit)
{
  range_t *r = malloc(sizeof(range_t));
  if (r == NULL) {
    ERROR(ERR_NO_MEM, "failed to alloc range struct for 0x%x-0x%x",
          base, limit);
    return NULL;
  }

  r->base = base;
  r->limit = limit;
  INIT_LIST_HEAD(&r->link);
  return r;
}

void
range_dump(ranges_t *ranges)
{
  range_t *range;

  list_for_each_entry(range, ranges, link) {
    mon_printf("  range 0x%08x-0x%08x\n",
               range->base, range->limit);
  }
}

void
range_add(ranges_t *ranges,
          uint32_t base,
          uint32_t limit)
{
  range_t *range;
  range_t *r = range_alloc(base, limit);

  BUG_ON(r == NULL, "range alloc");
  BUG_ON(base >= limit, "base (0x%x) >= limit (0x%x)",
         base, limit);

  list_for_each_entry(range, ranges, link) {
    BUG_ON(base >= range->base && limit <= range->limit,
           "range 0x%x-0x%x already present",
           base, limit);

    if (range->base > limit) {
      list_add_tail(&r->link, &range->link);
      return;
    }
  }

  list_add_tail(&r->link, ranges);
}

void
range_remove(ranges_t *ranges,
             uint32_t base,
             uint32_t limit)
{
  range_t *range;
  range_t *n;

  BUG_ON(base >= limit, "base (0x%x) >= limit (0x%x)",
         base, limit);

  list_for_each_entry_safe(range, n, ranges, link) {
    if (range->base > limit ||
        range->limit < base) {
      continue;
    } else if (range->base >= base &&
               range->limit <= limit) {
      /*
       * Entire range to be deleted.
       */
      list_del(&range->link);
      free(range);
    } else {
      if (range->base >= base) {
        /*
         * range->limit > limit.
         */
        range->base = limit + 1;
      } else if (range->limit <= limit) {
        /*
         * range->base < base.
         */
        range->limit = base - 1;
      } else {
        uint32_t b = range->base;
        /*
         * range->base < base.
         * range->limit > limit.
         */
        range->base = limit + 1;
        range_add(ranges, b, base - 1);
      }
    }
  }
}
