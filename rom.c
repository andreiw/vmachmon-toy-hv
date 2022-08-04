#include "guest.h"
#include "rom.h"
#include "ranges.h"
#include "pmem.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define CELL(x, i) (x + i * sizeof(cell_t))
#define NODE_MUNGE(x) (x + 0x10000000)
#define NODE_DEMUNGE(x) (x - 0x10000000)
#define PAGE2SP(x) ((void *)((x) + vm_page_size - C_RED_ZONE))

typedef uint32_t cell_t;
typedef cell_t phandle_t;
typedef cell_t ihandle_t;

static void *fdt;
static ranges_t guest_mem_avail_ranges;
static ranges_t guest_mem_reg_ranges;
static gra_t cif_trampoline;
static gra_t claim_arena_start;
static gra_t claim_arena_ptr;
static gra_t claim_arena_end;
static gra_t stack_start;
static gra_t stack_end;

static char string_buf[PAGE_SIZE];

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
rom_finddevice(gea_t cia)
{
  int node;
  err_t err;
  gea_t dev_ea;
  gea_t ihandle_ea;
  char *d = string_buf;
  uint32_t ihandle;

  err = guest_from(&dev_ea, CELL(cia, 3), sizeof(dev_ea));
  ON_ERROR("dev ea", err, done);

  err = guest_from(&ihandle_ea, CELL(cia, 4), sizeof(ihandle_ea));
  ON_ERROR("ihandle ea", err, done);

  d[guest_from_ex(d, dev_ea, sizeof(string_buf), true)] = '\0';
  node = fdt_path_offset(fdt, d);
  if (node < 0) {
    WARN("'%s' not found", d);
    ihandle = -1;
  } else {
    ihandle = NODE_MUNGE(node);
    VERBOSE("'%s' -> node %u ihandle 0x%x", d, node, ihandle);
  }

  err = guest_to(ihandle_ea, &ihandle, sizeof(ihandle));
  ON_ERROR("ihandle", err, done);

 done:
  return err;
}

err_t
rom_call(void)
{
  gra_t pc;
  err_t err;
  gea_t cia;
  gea_t service_ea;
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
  err = guest_from(&service_ea, CELL(cia, 0), sizeof(cia));
  ON_ERROR("service ea", err, done);

  err = guest_from(&in_count, CELL(cia, 1), sizeof(in_count));
  ON_ERROR("in count", err, done);

  err = guest_from(&out_count, CELL(cia, 2), sizeof(out_count));
  ON_ERROR("out count", err, done);

  service[guest_from_ex(&service, service_ea, sizeof(service) - 1, true)] = '\0';
  VERBOSE("OF call '%s' from 0x%x cia 0x%x in %u out %u",
          service, r->ppcLR, cia, in_count, out_count);

  err = ERR_UNSUPPORTED;
  if (!strcmp("finddevice", service)) {
    err = rom_finddevice(cia);
  } else {
    WARN("unsupported CIF service '%s'", service);
  }

done:
  if (err == ERR_NONE) {
    r->ppcGPRs[3] = 0;
  } else {
    r->ppcGPRs[3] = -1;
  }

  r->ppcPC = r->ppcLR;
  return ERR_NONE;
}
