#define LOG_PFX PMEM
#include "pvp.h"

static ha_t pmem;
static size_t pmem_bytes;

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

size_t pmem_size()
{
  return pmem_bytes;
}

bool pmem_gra_valid(gra_t ra)
{
  return ra < pmem_bytes;
}

err_t pmem_init(size_t bytes)
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

void
pmem_to(gra_t dest, void *src, size_t bytes)
{
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
}

void
pmem_from(void *dest, gra_t src, size_t bytes)
{
  if (guest_is_little()) {
    unsigned i;
    uint8_t *s = (void *) (pmem + src);
    uint8_t *d = dest;

    for (i = 0; i < bytes; i++) {
      d[i] = s[i ^ 7];
    }
  } else {
    memcpy(dest, (void *) (pmem + src), bytes);
  }
}
