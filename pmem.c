#include "pvp.h"
#define LOG_PFX PMEM
#include "log.h"

static vm_address_t pmem;
static size_t pmem_bytes;

vm_address_t pmem_base()
{
  return pmem;
}

size_t pmem_size()
{
  return pmem_bytes;
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
  memcpy((void *) (pmem + dest), src, bytes);
}

void
pmem_from(void *dest, gra_t src, size_t bytes)
{
  memcpy(dest, (void *) (pmem + src), bytes);
}
