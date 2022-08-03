#include "guest.h"
#include "rom.h"
#include "ranges.h"
#include "pmem.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define PAGE2SP(x) ((void *)((x) + vm_page_size - C_RED_ZONE))

static void *fdt;
static ranges_t guest_mem_avail_ranges;
static ranges_t guest_mem_reg_ranges;
static gra_t cif_trampoline;
static gra_t claim_arena_start;
static gra_t claim_arena_ptr;
static gra_t claim_arena_end;
static gra_t stack_start;
static gra_t stack_end;

uint32_t
rom_claim(uint32_t addr,
          uint32_t size,
          uint32_t align)
{
  uint32_t out;

  if (align != 0) {
    claim_arena_ptr =
      ALIGN_UP(claim_arena_ptr, align);

    BUG_ON(claim_arena_ptr >=
           claim_arena_end, "claim arena overflow");

    out = (uint32_t) claim_arena_ptr;
    claim_arena_ptr += size;

    BUG_ON(claim_arena_ptr >
           claim_arena_end, "claim arena overflow");
  } else {
    BUG_ON(addr + size > pmem_size(), "addr outside pmem");
    out = addr;
  }

  /*
   * Internally we keep track of all allocations at page granularity,
   * due to some buggy handling of non-page aligned ranges in veneer.
   */
  range_remove(&guest_mem_avail_ranges,
               ALIGN(out, PAGE_SIZE),
               ALIGN_UP(out + size, PAGE_SIZE) - 1);
  return out;
}

err_t
rom_init(const char *fdt_path)
{
  int fd;
  int ret;
  gea_t loader_base;
  length_t loader_length;
  gea_t stack_base = 0;
  struct stat st;
  const char *loader = "iquik.b";

  BUG_ON(pmem_size() <= MB(16), "guest RAM too small");

  range_init(&guest_mem_avail_ranges);
  range_init(&guest_mem_reg_ranges);
  range_add(&guest_mem_avail_ranges, 0,  pmem_size() - 1);
  range_add(&guest_mem_reg_ranges, 0, pmem_size() - 1);

  claim_arena_start = pmem_size() - MB(16);
  claim_arena_ptr = claim_arena_start;
  claim_arena_end = pmem_size();

  loader_base = 0x3e0000;
  stack_base = 2 * vm_page_size;
  uint32_t hvcall = 0x0f040000;

  fd = open(loader, O_RDONLY);
  if (fd < 0) {
    POSIX_ERROR(errno, "could not open loader '%s'", loader);
    goto posix_err;
  }

  ret = fstat(fd, &st);
  ON_POSIX_ERROR("stat", ret, posix_err);
  loader_length = st.st_size;

  ret = read(fd, (void *) pmem_ha(loader_base), loader_length);
  ON_POSIX_ERROR("read", ret, posix_err);
  close(fd);

  rom_claim(loader_base, loader_length, 0);

  fd = open(fdt_path, O_RDONLY);
  if (fd < 0) {
    POSIX_ERROR(errno, "could not open device tree '%s'", fdt_path);
    goto posix_err;
  }

  ret = fstat(fd, &st);
  ON_POSIX_ERROR("fdt stat", ret, posix_err);

  fdt = malloc(st.st_size);
  if (fdt == NULL) {
    ERROR(errno, "couldn't alloc %u bytes to read fdt", st.st_size);
    goto posix_err;
  }

  ret = read(fd, fdt, st.st_size);
  ON_POSIX_ERROR("fdt read", ret, posix_err);
  close(fd);

  /*
   * CIF entry.
   */
  cif_trampoline = 0x4;
  pmem_to(cif_trampoline, &hvcall, sizeof(hvcall));
  rom_claim(cif_trampoline, sizeof(hvcall), 0);

  /*
   * Stack.
   */
  stack_end = claim_arena_start;
  stack_start = stack_end - MB(1);
  rom_claim(stack_start, stack_end - stack_start, 0);

  /*
   * Prep CPU state.
   */
  guest->regs->ppcPC = loader_base;
  guest->regs->ppcGPRs[1] = stack_end - C_RED_ZONE;
  guest->regs->ppcGPRs[5] = cif_trampoline;

  return ERR_NONE;
 posix_err:
  return ERR_POSIX;
}

err_t
rom_call(void)
{
  gra_t pc;
  err_t err;
  gea_t cia;
  gea_t call_ea;
  uint32_t in_count;
  uint32_t out_count;
  char service[32];
  vmm_regs32_t *r = guest->regs;

  err = guest_backmap(r->ppcPC, &pc);
  if (err != ERR_NONE) {
    return ERR_UNSUPPORTED;
  }

  if (pc != cif_trampoline) {
    return ERR_UNSUPPORTED;
  }

  cia = r->ppcGPRs[3];
  if (guest_from(&call_ea, cia + 0, sizeof(cia)) != sizeof(cia)) {
    WARN("could not access cia");
    goto done;
  }

  if (guest_from(&in_count, cia + 4, sizeof(in_count)) != sizeof(in_count)) {
    WARN("could not acces in_count");
    goto done;
  }

  if (guest_from(&out_count, cia + 8, sizeof(out_count)) != sizeof(out_count)) {
    WARN("could not acces out_count");
    goto done;
  }

  service[guest_from(&service, call_ea, sizeof(service))] = '\0';
  VERBOSE("OF call %s from 0x%x in %u out %u",
          service, r->ppcLR, in_count, out_count);

done:
  r->ppcPC = r->ppcLR;
  return ERR_NONE;
}
