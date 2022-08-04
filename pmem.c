#define LOG_PFX PMEM
#include "pvp.h"

static ha_t pmem;
static length_t pmem_bytes;

ha_t pmem_ha(gra_t ra)
{
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
pmem_to(gra_t dest, void *src, length_t bytes)
{
  if (dest >= pmem_bytes) {
    return 0;
  }

  if (dest + bytes > pmem_bytes) {
    bytes = pmem_bytes - dest;
  }

  if (guest_is_little()) {
    unsigned i;
    uint8_t *s = src;
    uint8_t *d = (void *) (pmem + dest);

    for (i = 0; i < bytes; i++) {
      d[i ^ 7] = s[i];
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
             bool nul_term)
{
  /*
   * If nul_term is set, will return bytes copied
   * *not* including the NUL (but the NUL will be
   * copied).
   */

  if (src >= pmem_bytes) {
    return 0;
  }

  if (src + bytes > pmem_bytes) {
    bytes = pmem_bytes - src;
  }

  if (guest_is_little()) {
    unsigned i;
    uint8_t *s = (void *) (pmem + src);
    uint8_t *d = dest;

    for (i = 0; i < bytes; i++) {
      d[i] = s[i ^ 7];

      if (nul_term && d[i] == 0) {
        return i;
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
pmem_from(void *dest, gra_t src, length_t bytes)
{
  return pmem_from_ex(dest, src, bytes, false);
}
