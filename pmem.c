#define LOG_PFX PMEM
#include "pvp.h"
#include "guest.h"
#include "pmem.h"

static ha_t pmem;
static length_t pmem_bytes;

ha_t pmem_ha(gra_t ra)
{
  BUG_ON(ra >= pmem_bytes, "bad ra");
  return pmem + ra;
}

err_t pmem_gra(ha_t ha, gra_t *gra)
{
  if (ha >= pmem && ha < (pmem + pmem_bytes)) {
    *gra = ha - pmem;
    return ERR_NONE;
  }

  return ERR_NOT_FOUND;
}

length_t pmem_size()
{
  return pmem_bytes;
}

bool pmem_gra_valid(gra_t ra)
{
  return ra < pmem_bytes;
}

err_t pmem_init(length_t bytes)
{
  mach_port_t mt;
  kern_return_t kr;
  pmem_bytes = ALIGN_UP(bytes, vm_page_size);

  mt = mach_task_self();
  kr = vm_allocate(mt, &pmem, pmem_bytes, VM_FLAGS_ANYWHERE);
  ON_MACH_ERROR("pmem_init vm_allocate", kr, err);

  return ERR_NONE;
err:
  pmem = 0;
  pmem_bytes = 0;
  return ERR_MACH;
}

length_t
pmem_to(gra_t dest,
        const void *src,
        length_t bytes,
        length_t access_size)
{
  if (dest >= pmem_bytes) {
    return 0;
  }

  if (dest + bytes > pmem_bytes) {
    bytes = pmem_bytes - dest;
  }

  BUG_ON(bytes % access_size != 0, "bad alignment");
  BUG_ON(access_size != 1 && access_size != 2 && access_size != 4,
         "bad access_size");

  if (guest_is_little()) {
    unsigned i;
    ha_t d = pmem + dest;

    for (i = 0; i < bytes; i += access_size) {
      if (access_size == 1) {
        *(uint8_t *)((d + i) ^ 7) = *(uint8_t *)(src + i);
      } else if (access_size == 2) {
        *(uint16_t *)((d + i) ^ 6) = *(uint16_t *)(src + i);
      } else if (access_size == 4) {
        *(uint32_t *)((d + i) ^ 4) = *(uint32_t *)(src + i);
      }
    }
  } else {
    memcpy((void *) (pmem + dest), src, bytes);
  }

  return bytes;
}

length_t
pmem_from_ex(void *dest,
             gra_t src,
             length_t bytes,
             length_t access_size,
             uint32_t flags)
{
  /*
   * If nul_term is set, will return bytes copied
   * *not* including the NUL (but the NUL will be
   * copied).
   */
  bool nul_term = (flags & PMEM_FROM_NUL_TERM) != 0;
  bool force_be = (flags & PMEM_FROM_FORCE_BE) != 0;

  if (src >= pmem_bytes) {
    return 0;
  }

  if (src + bytes > pmem_bytes) {
    bytes = pmem_bytes - src;
  }

  BUG_ON(bytes % access_size != 0, "bad alignment");
  BUG_ON(access_size != 1 && access_size != 2 && access_size != 4,
         "bad access_size");

  if (guest_is_little() && !force_be) {
    unsigned i;
    ha_t s = pmem + src;

    for (i = 0; i < bytes; i += access_size) {
      if (access_size == 1) {
        uint8_t v;
        v = *(uint8_t *)(dest + i) = *(uint8_t *) ((s + i) ^ 7);

        if (nul_term && v == 0) {
          return i;
        }
      } else if (access_size == 2) {
        *(uint16_t *)(dest + i) = *(uint16_t *) ((s + i) ^ 6);
      } else if (access_size == 4) {
        *(uint32_t *)(dest + i) = *(uint32_t *) ((s + i) ^ 4);
      }
    }
  } else {
    if (nul_term) {
      return strlcpy(dest, (void *) pmem + src, bytes);
    } else {
      memcpy(dest, (void *) (pmem + src), bytes);
    }
  }

  return bytes;
}

length_t
pmem_from(void *dest,
          gra_t src,
          length_t bytes,
          length_t access_size)
{
  return pmem_from_ex(dest, src, bytes, access_size, 0);
}

