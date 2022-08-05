#include "guest.h"
#include "rom.h"
#include "ranges.h"
#include "pmem.h"
#include "libfdt.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define CELL(x, i) (x + i * sizeof(cell_t))
#define NODE_MUNGE(x) (x + 0x10000000)
#define NODE_DEMUNGE(x) (x - 0x10000000)

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

static char xfer_buf[PAGE_SIZE];

typedef struct {
  err_t (*handler)(gea_t cia);
  const char *name;
} cif_handler_t;

static uint32_t
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
  gea_t loader_entry;
  length_t loader_length;
  length_t loaded_length;
  struct stat st;
  const char *loader;
  void *loader_data;
  gea_t stack_base;
  err_t err = ERR_NONE;

  BUG_ON(pmem_size() <= MB(16), "guest RAM too small");

  range_init(&guest_mem_avail_ranges);
  range_init(&guest_mem_reg_ranges);
  range_add(&guest_mem_avail_ranges, 0,  pmem_size() - 1);
  range_add(&guest_mem_reg_ranges, 0, pmem_size() - 1);

  claim_arena_start = pmem_size() - MB(16);
  claim_arena_ptr = claim_arena_start;
  claim_arena_end = pmem_size();

  if (guest_is_little()) {
    loader = "veneer.exe";
    /*
     * Skip COFF header.
     */
    loader_entry = 0x50000;
    loader_base = loader_entry - 0x200;
  } else {
    loader = "iquik.b";
    loader_entry = loader_base = 0x3e0000;
  }
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

  loader_data = malloc(loader_length);
  if (loader_data == NULL) {
    POSIX_ERROR(errno, "could not alloc temp buffer for loader '%s'", loader);
    goto posix_err;
  }

  ret = read(fd, loader_data, loader_length);
  ON_POSIX_ERROR("read", ret, posix_err);
  close(fd);
  loaded_length = pmem_to(loader_base, loader_data, loader_length, 1);
  free(loader_data);
  if (loader_length != loaded_length) {
    err = ERR_BAD_ACCESS;
    ERROR(err, "could not copy in the loader");
    goto done;
  }

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
  pmem_to(cif_trampoline, &hvcall, sizeof(hvcall), 4);
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
  guest->regs->ppcPC = loader_entry;
  guest->regs->ppcGPRs[1] = stack_end - C_RED_ZONE;
  guest->regs->ppcGPRs[5] = cif_trampoline;
 done:
  return err;
 posix_err:
  return ERR_POSIX;
}

static err_t
rom_getprop_ex(int node,
               const char *name,
               cell_t len_in,
               gea_t *data_ea,
               cell_t *len_out)
{
  int len;
  err_t err;
  const void *data;

  if (!strcmp(name, "name")) {
    if (node == 0) {
      data = "/";
      len = 1;
    } else {
      data = fdt_get_name(fdt, node, &len);
    }
  } else {
    data = fdt_getprop(fdt, node, name, &len);
  }

  if (data == NULL) {
    WARN("property '%s' not found in node %u", name, node);
    return ERR_NOT_FOUND;
  }

  *len_out = len;
  if (data_ea != NULL) {
    err = guest_to(*data_ea, data, len, 1);
    ON_ERROR("data", err, done);
  }

 done:
  return err;
}

static err_t
rom_getprop(gea_t cia)
{
  err_t err;
  phandle_t phandle;
  gea_t prop_ea;
  gea_t data_ea;
  cell_t len_in;
  cell_t len_out;
  char *p = xfer_buf;

  err = guest_from_x(&phandle, CELL(cia, 3));
  ON_ERROR("phandle", err, done);

  err = guest_from_x(&prop_ea, CELL(cia, 4));
  ON_ERROR("prop ea", err, done);

  err = guest_from_x(&data_ea, CELL(cia, 5));
  ON_ERROR("data ea", err, done);

  err = guest_from_x(&len_in, CELL(cia, 6));
  ON_ERROR("len in", err, done);

  p[guest_from_ex(p, prop_ea, sizeof(xfer_buf), 1, true)] = '\0';
  err = rom_getprop_ex(NODE_DEMUNGE(phandle), p, len_in,
                       &data_ea, &len_out);
  ON_ERROR("rom_getprop_ex", err, done);

  err = guest_to_x(CELL(cia, 7), &len_out);
  ON_ERROR("len_out", err, done);

 done:
  return err;
}

static err_t
rom_finddevice(gea_t cia)
{
  int node;
  err_t err;
  gea_t dev_ea;
  char *d = xfer_buf;
  phandle_t phandle;

  err = guest_from_x(&dev_ea, CELL(cia, 3));
  ON_ERROR("dev ea", err, done);

  d[guest_from_ex(d, dev_ea, sizeof(xfer_buf), 1, true)] = '\0';
  node = fdt_path_offset(fdt, d);
  if (node < 0) {
    WARN("dev '%s' not found", d);
    phandle = -1;
  } else {
    phandle = NODE_MUNGE(node);
  }

  err = guest_to_x(CELL(cia, 4), &phandle);
  ON_ERROR("phandle", err, done);

 done:
  return err;
}

static err_t
rom_write(gea_t cia)
{
  err_t err;
  ihandle_t ihandle;
  gea_t data_ea;
  cell_t len_in;
  cell_t len_out;

  err = guest_from_x(&ihandle, CELL(cia, 3));
  ON_ERROR("ihandle", err, done);

  err = guest_from_x(&data_ea, CELL(cia, 4));
  ON_ERROR("data ea", err, done);

  err = guest_from_x(&len_in, CELL(cia, 5));
  ON_ERROR("data ea", err, done);

  len_out = len_in;
  do {
    length_t xfer = min(len_in, (sizeof(xfer_buf) - 1));

    err = guest_from(xfer_buf, data_ea, xfer, 1);
    ON_ERROR("data", err, partial);

    xfer_buf[xfer] = '\0';
    fprintf(stderr, xfer_buf);

    len_in -= xfer;
    data_ea += xfer;
  } while (len_in != 0);

 partial:
  if (err == ERR_BAD_ACCESS) {
    err = ERR_NONE;
  }

  if (err == ERR_NONE) {
    len_out -= len_in;
    err = guest_to_x(CELL(cia, 6), &len_out);
    ON_ERROR("len_out", err, done);
  }

 done:
  return err;
}

cif_handler_t handlers[] = {
  { rom_finddevice, "finddevice" },
  { rom_getprop, "getprop" },
  { rom_write, "write" }
};

err_t
rom_call(void)
{
  gra_t pc;
  err_t err;
  gea_t cia;
  unsigned i;
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
  err = guest_from_x(&service_ea, CELL(cia, 0));
  ON_ERROR("service ea", err, done);

  err = guest_from_x(&in_count, CELL(cia, 1));
  ON_ERROR("in count", err, done);

  err = guest_from_x(&out_count, CELL(cia, 2));
  ON_ERROR("out count", err, done);

  service[guest_from_ex(&service, service_ea, sizeof(service) - 1, 1, true)] = '\0';

  err = ERR_UNSUPPORTED;
  for (i = 0; i < ARRAY_LEN(handlers); i++) {
    if (!strcmp(service, handlers[i].name)) {
      err = handlers[i].handler(cia);
      break;
    }
  }

  if (i == ARRAY_LEN(handlers)) {
    WARN("Unsupported OF call '%s' from 0x%x cia 0x%x in %u out %u",
          service, r->ppcLR, cia, in_count, out_count);
    return ERR_SHUTDOWN;
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
