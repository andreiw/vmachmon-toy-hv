#include "pvp.h"
#include "mmu_ranges.h"

mmu_range_t *
mmu_range_alloc(uint32_t base,
                uint32_t limit,
                gra_t ra,
                uint32_t flags)
{
  mmu_range_t *r = malloc(sizeof(mmu_range_t));
  if (r == NULL) {
    ERROR(ERR_NO_MEM, "failed to alloc mmu_range struct for 0x%x-0x%x",
          base, limit);
    return NULL;
  }

  r->base = base;
  r->limit = limit;
  r->ra =ra;
  r->flags = flags;
  INIT_LIST_HEAD(&r->link);
  return r;
}

void
mmu_range_dump(mmu_ranges_t *mmu_ranges)
{
  mmu_range_t *mmu_range;

  list_for_each_entry(mmu_range, mmu_ranges, link) {
    LOG("mmu_range 0x%x-0x%x -> 0x%x-0x%x",
        mmu_range->ra, mmu_range->limit - mmu_range->base +
        mmu_range->ra,
        mmu_range->base, mmu_range->limit);
  }
}

void
mmu_range_add(mmu_ranges_t *mmu_ranges,
              uint32_t base,
              uint32_t limit,
              gra_t ra,
              uint32_t flags)
{
  mmu_range_t *mmu_range;
  mmu_range_t *r = mmu_range_alloc(base, limit, ra, flags);

  BUG_ON(r == NULL, "mmu_range alloc");
  BUG_ON(base >= limit, "base (0x%x) >= limit (0x%x)",
         base, limit);

  list_for_each_entry(mmu_range, mmu_ranges, link) {
    if (base >= mmu_range->base && limit <= mmu_range->limit) {
      BUG_ON ((base - mmu_range->base + mmu_range->ra != ra) ||
              flags != mmu_range->flags,
              "incompatible overlapping range");
    } else {
      BUG_ON (limit >= mmu_range->base && base <= mmu_range->limit,
              "partially overlapping range"); 
    }

    if (mmu_range->base > limit) {
      list_add_tail(&r->link, &mmu_range->link);
      return;
    }
  }

  list_add_tail(&r->link, mmu_ranges);
}

mmu_range_t *
mmu_range_find(mmu_ranges_t *mmu_ranges,
               gea_t ea)
{
  mmu_range_t *mmu_range;

  list_for_each_entry(mmu_range, mmu_ranges, link) {
    if (ea >= mmu_range->base &&
        ea <= mmu_range->limit) {
      return mmu_range;
    }
  }

  return NULL;
}
