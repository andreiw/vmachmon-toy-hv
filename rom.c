/*
 * This either loads iquik.b (BE) or veneer.exe (LE) and
 * emulates just enough OF CIF. Because I am lazy, this uses
 * libfdt and a DTB blob. Given these circumstances,
 * ihandles and phandles are equivalent.
 */

#define LOG_PFX ROM
#include "guest.h"
#include "rom.h"
#include "ranges.h"
#include "pmem.h"
#include "libfdt.h"
#include "term.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#define PHANDLE_MUNGE 0x10000000
#define ROOT_PHANDLE rom_get_phandle(0)
#define CELL(x, i) (x + i * sizeof(cell_t))

typedef uint32_t cell_t;
typedef cell_t phandle_t;
typedef cell_t ihandle_t;

static void *fdt;
static int memory_node;
static ihandle_t mmu_ihandle;
static ihandle_t memory_ihandle;
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
  err_t (*handler)(gea_t cia, count_t in, count_t out);
  const char *name;
} cif_handler_t;

static phandle_t
rom_get_phandle(int node)
{
  return node | PHANDLE_MUNGE;
}

static ihandle_t
rom_get_ihandle(int node)
{
  return fdt_get_phandle(fdt, node);
}

static ihandle_t
rom_path_to_ihandle(const char *path)
{
  int node = fdt_path_offset(fdt, path);
  if (node == -1) {
    return -1;
  }

  return rom_get_ihandle(node);
}

static int
rom_node_offset_by_phandle(phandle_t phandle)
{
  return phandle &= ~PHANDLE_MUNGE;
}

static int
rom_node_offset_by_ihandle(ihandle_t ihandle)
{
  return fdt_node_offset_by_phandle(fdt, ihandle);
}

static uint32_t
rom_claim_ex(uint32_t addr,
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
rom_milliseconds(gea_t cia,
                 count_t in,
                 count_t out)
{
  err_t err;
  cell_t ms;
  struct timeval te;
  gettimeofday(&te, NULL);

  ms = te.tv_sec * 1000 + te.tv_usec / 1000;
  err = guest_to_x(CELL(cia, 3), &ms);
  ON_ERROR("out", err, done);

 done:
  return err;
}

err_t
rom_claim(gea_t cia,
          count_t in_count,
          count_t out_count)
{
  err_t err;
  cell_t addr;
  cell_t size;
  cell_t align;
  cell_t out;

  err = guest_from_x(&addr, CELL(cia, 3));
  ON_ERROR("addr", err, done);

  err = guest_from_x(&size, CELL(cia, 4));
  ON_ERROR("size", err, done);

  err = guest_from_x(&align, CELL(cia, 5));
  ON_ERROR("align", err, done);

  out = rom_claim_ex(addr, size, align);

  err = guest_to_x(CELL(cia, 6), &out);
  ON_ERROR("out", err, done);
 done:
  return err;
}

err_t
rom_callmethod(gea_t cia,
               count_t in,
               count_t out)
{
  err_t err;
  gea_t method_ea;
  ihandle_t ihandle;
  char *call;

  err = guest_from_x(&method_ea, CELL(cia, 3));
  ON_ERROR("method ea", err, done);

  err = guest_from_x(&ihandle, CELL(cia, 4));
  ON_ERROR("ihandle", err, done);

  call = xfer_buf;
  call[guest_from_ex(call, method_ea, sizeof(xfer_buf) - 1, 1, true)] = '\0';

  err = ERR_UNSUPPORTED;
  if (ihandle == memory_ihandle && !strcmp("claim", call)) {
    cell_t align;
    cell_t size;
    gra_t addr;
    cell_t result;

    err = guest_from_x(&align, CELL(cia, 5));
    ON_ERROR("mclaim align", err, done);

    err = guest_from_x(&size, CELL(cia, 6));
    ON_ERROR("mclaim size", err, done);

    err = guest_from_x(&addr, CELL(cia, 7));
    ON_ERROR("mclaim addr", err, done);

    result = rom_claim_ex(addr, size, align);

    err = guest_to_x(CELL(cia, 9), &result);
    ON_ERROR("mclaim result", err, done);

    result = 0;
    err = guest_to_x(CELL(cia, 8), &result);
    ON_ERROR("mclaim outer result", err, done);
  } else if (ihandle == mmu_ihandle && !strcmp("map", call)) {
    gra_t phys;
    gea_t virt;
    cell_t size;
    cell_t mode;
    cell_t result;

    err = guest_from_x(&mode, CELL(cia, 5));
    ON_ERROR("mmap mode", err, done);

    err = guest_from_x(&size, CELL(cia, 6));
    ON_ERROR("mmap size", err, done);

    err = guest_from_x(&virt, CELL(cia, 7));
    ON_ERROR("mmap virt", err, done);

    err = guest_from_x(&phys, CELL(cia, 8));
    ON_ERROR("mmap phys", err, done);

    if (mode != -1) {
      WARN("mmu map phys 0x%x virt 0x%x size 0x%x mode 0x%x",
           phys, virt, size, mode);
    }

    result = 0;
    if (phys != virt) {
      if (pmem_gra_valid(phys)) {
        ha_t ha;
        ha = pmem_ha(phys);

        while (size != 0) {
          err = guest_map(ha, virt);
          if (err != ERR_NONE) {
            result = -0;
            break;
          }

          ha += PAGE_SIZE;
          virt += PAGE_SIZE;
          size -= PAGE_SIZE;
        }
      } else {
        result = -1;
      }
    }

    err = guest_to_x(CELL(cia, 9), &result);
    ON_ERROR("mmap outer result", err, done);
  } else {
    ERROR(err, "Unknown method '%s' on ihandle 0x%x",
          call, ihandle);
  }

 done:
  return err;
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
  uint32_t hvcall = 0x44000022; /* sc 1 */

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

  rom_claim_ex(loader_base, loader_length, 0);

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

  memory_node = fdt_path_offset(fdt, "mem");
  BUG_ON(memory_node == -1, "memory node missing from DT template");
  memory_ihandle = rom_get_ihandle(memory_node);

  mmu_ihandle = rom_path_to_ihandle("mmu");
  BUG_ON(mmu_ihandle == -1, "mmu node missing from DT template");

  /*
   * CIF entry.
   */
  cif_trampoline = 0x4;
  pmem_to(cif_trampoline, &hvcall, sizeof(hvcall), 4);
  rom_claim_ex(cif_trampoline, sizeof(hvcall), 0);

  /*
   * Stack.
   */
  stack_end = claim_arena_start;
  stack_start = stack_end - MB(1);
  rom_claim_ex(stack_start, stack_end - stack_start, 0);

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
  const void *data;
  err_t err = ERR_NONE;

  if (!strcmp(name, "name")) {
    if (node == 0) {
      data = "/";
      len = 1;
    } else {
      data = fdt_get_name(fdt, node, &len);
    }
  } else if (node == memory_node &&
             !strcmp(name, "reg")) {
    len = range_count(&guest_mem_reg_ranges) *
      sizeof(cell_t) * 2;

    // XXX: is this working around a veneer bug?
    if (len_in == 0) {
      *len_out = len;
    } else {
      *len_out = min(len, len_in);
    }

    if (data_ea != NULL) {
      range_t *r;
      gea_t ea = *data_ea;

      list_for_each_entry(r, &guest_mem_reg_ranges, link) {
        length_t l;

        if (len < sizeof(cell_t)) {
          break;
        }

        WARN("writing base 0x%x to ea 0x%x", r->base, ea);
        err = guest_to(ea, &r->base, sizeof(cell_t), 1);
        ON_ERROR("base", err, done);

        len -= sizeof(cell_t);
        ea += sizeof(cell_t);

        if (len < sizeof(cell_t)) {
          break;
        }

        l = r->limit - r->base + 1;
        WARN("writing size 0x%x to ea 0x%x", l, ea);
        err = guest_to(ea, &l, sizeof(cell_t), 1);
        ON_ERROR("size", err, done);

        len -= sizeof(cell_t);
        ea += sizeof(cell_t);
      }
    }

    return ERR_NONE;
  } else if (node == memory_node &&
             !strcmp(name, "available")) {
    len = range_count(&guest_mem_avail_ranges) *
      sizeof(cell_t) * 2;

    // XXX: is this working around a veneer bug?
    if (len_in == 0) {
      *len_out = len;
    } else {
      *len_out = min(len, len_in);
    }

    if (data_ea != NULL) {
      range_t *r;
      gea_t ea = *data_ea;

      list_for_each_entry(r, &guest_mem_avail_ranges, link) {
        /*
         * Somehow veneer really doesn't like non-page aligned
         * quantities in the "available" property.
         */
        cell_t b = r->base;
        cell_t bs = r->limit - r->base + 1;

        BUG_ON(b % PAGE_SIZE != 0, "unexpected avail range 0x%x-0x%x",
               r->base, r->limit);
        BUG_ON(bs % PAGE_SIZE != 0, "unexpected avail range 0x%x-0x%x",
               r->base, r->limit);

        if (len < sizeof(cell_t)) {
          break;
        }

        WARN("writing base 0x%x to ea 0x%x", b, ea);
        err = guest_to(ea, &b, sizeof(cell_t), 1);
        ON_ERROR("base", err, done);

        len -= sizeof(cell_t);
        ea += sizeof(cell_t);

        if (len < sizeof(cell_t)) {
          break;
        }

        WARN("writing base 0x%x to ea 0x%x", bs, ea);
        err = guest_to(ea, &bs, sizeof(cell_t), 1);
        ON_ERROR("size", err, done);

        len -= sizeof(cell_t);
        ea += sizeof(cell_t);
      }
    }

    return ERR_NONE;
  } else {
    data = fdt_getprop(fdt, node, name, &len);
  }

  if (data == NULL) {
    WARN("property '%s' not found in node %u (%s)", name,
         node, fdt_get_name(fdt, node, NULL));
    return ERR_NOT_FOUND;
  }

  *len_out = len;
  if (data_ea != NULL) {
    err = guest_to(*data_ea, data, min(len, len_in), 1);
    ON_ERROR("data", err, done);
  }

 done:
  return err;
}

static err_t
rom_getprop(gea_t cia,
            count_t in,
            count_t out)
{
  int node;
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

  node = rom_node_offset_by_phandle(phandle);
  if (node < 0) {
    err = ERR_NOT_FOUND;
    WARN("looking up '%s' in unknown phandle 0x%x",
         p, phandle);
    goto done;
  }

  err = rom_getprop_ex(node, p, len_in,
                       &data_ea, &len_out);
  if (err != ERR_NONE) {
    goto done;
  }

  err = guest_to_x(CELL(cia, 7), &len_out);
  ON_ERROR("len_out", err, done);

 done:
  return err;
}

static err_t
rom_getproplen(gea_t cia,
               count_t in,
               count_t out)
{
  int node;
  err_t err;
  phandle_t phandle;
  gea_t prop_ea;
  cell_t len_out;
  char *p = xfer_buf;

  err = guest_from_x(&phandle, CELL(cia, 3));
  ON_ERROR("phandle", err, done);

  err = guest_from_x(&prop_ea, CELL(cia, 4));
  ON_ERROR("prop ea", err, done);

  p[guest_from_ex(p, prop_ea, sizeof(xfer_buf), 1, true)] = '\0';

  node = rom_node_offset_by_phandle(phandle);
  if (node < 0) {
    err = ERR_NOT_FOUND;
    WARN("looking up unknown phandle 0x%x", phandle);
    goto done;
  }

  err = rom_getprop_ex(node, p, 0,
                       NULL, &len_out);
  if (err != ERR_NONE) {
    goto done;
  }

  err = guest_to_x(CELL(cia, 5), &len_out);
  ON_ERROR("len_out", err, done);

 done:
  return err;
}

static err_t
rom_child(gea_t cia,
          count_t in,
          count_t out)
{
  int node;
  err_t err;
  phandle_t ph;
  /*
   * 'child' always returns 0 on failure.
   */
  phandle_t ph_child = 0;

  err = guest_from_x(&ph, CELL(cia, 3));
  ON_ERROR("ph", err, done);

  node = rom_node_offset_by_phandle(ph);
  if (node < 0) {
    WARN("looking up unknown phandle 0x%x", ph);
    goto done;
  }

  node = fdt_subnode(fdt, node);
  if (node < 0) {
    goto done;
  } else {
    ph_child = rom_get_phandle(node);
  }

 done:
  err = guest_to_x(CELL(cia, 4), &ph_child);
  ON_ERROR("out ph", err, done);
  return err;
}

static err_t
rom_peer(gea_t cia,
         count_t in,
         count_t out)
{
  int node;
  err_t err;
  phandle_t ph;
  /*
   * 'peer' always returns 0 on failure.
   */
  phandle_t ph_peer = 0;

  err = guest_from_x(&ph, CELL(cia, 3));
  ON_ERROR("ph", err, done);

  if (ph == 0) {
    ph_peer = ROOT_PHANDLE;
    goto done;
  }

  node = rom_node_offset_by_phandle(ph);
  if (node < 0) {
    WARN("looking up unknown phandle 0x%x", ph);
    goto done;
  }

  node = fdt_sibling(fdt, node);
  if (node < 0) {
    goto done;
  } else {
    ph_peer = rom_get_phandle(node);
  }

 done:
  err = guest_to_x(CELL(cia, 4), &ph_peer);
  ON_ERROR("out ph", err, done);
  return err;
}

static err_t
rom_parent(gea_t cia,
           count_t in,
           count_t out)
{
  int node;
  err_t err;
  phandle_t ph;
  phandle_t ph_parent;

  err = guest_from_x(&ph, CELL(cia, 3));
  ON_ERROR("ph", err, done);

  if (ph == ROOT_PHANDLE) {
    ph_parent = 0;
    goto done;
  }

  node = rom_node_offset_by_phandle(ph);
  if (node < 0) {
    WARN("looking up unknown phandle 0x%x", ph);
    goto done;
  }

  node = fdt_parent_offset(fdt, node);
  if (node < 0) {
    goto done;
  } else {
    ph_parent = rom_get_phandle(node);
  }

 done:
  err = guest_to_x(CELL(cia, 4), &ph_parent);
  ON_ERROR("out ph", err, done);
  return err;
}

static err_t
rom_itopackage(gea_t cia,
               count_t in,
               count_t out)
{
  err_t err;
  int node;
  ihandle_t ih;
  phandle_t ph;

  err = guest_from_x(&ih, CELL(cia, 3));
  ON_ERROR("ih", err, done);

  node = rom_node_offset_by_ihandle(ih);
  if (node < 0) {
    ph = -1;
  } else {
    ph = rom_get_phandle(node);
  }

  err = guest_to_x(CELL(cia, 4), &ph);
  ON_ERROR("ph", err, done);

 done:
  return err;
}

static err_t
rom_finddevice(gea_t cia,
               count_t in,
               count_t out)
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
    phandle = rom_get_phandle(node);
  }

  err = guest_to_x(CELL(cia, 4), &phandle);
  ON_ERROR("phandle", err, done);

 done:
  return err;
}

static err_t
rom_read(gea_t cia,
         count_t in,
         count_t out)
{
  err_t err;
  ihandle_t ihandle;
  cell_t data_ea;
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
    length_t xferred;
    length_t xfer = min(len_in, sizeof(xfer_buf));

    xferred = term_in(xfer_buf, xfer);

    err = guest_to(data_ea, xfer_buf, xferred, 1);
    ON_ERROR("data", err, partial);

    if (xferred != xfer) {
      err = ERR_NOT_READY;
      goto partial;
    }

    len_in -= xfer;
    data_ea += xfer;
  } while (len_in != 0);

 partial:
  if (err == ERR_BAD_ACCESS || err == ERR_NOT_READY) {
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

static err_t
rom_write(gea_t cia,
          count_t in,
          count_t out)
{
  err_t err;
  ihandle_t ihandle;
  cell_t data_ea;
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
    length_t xfer = min(len_in, sizeof(xfer_buf));

    err = guest_from(xfer_buf, data_ea, xfer, 1);
    ON_ERROR("data", err, partial);

    term_out(xfer_buf, xfer);

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

static err_t
rom_shutdown(gea_t cia,
             count_t in,
             count_t out)
{
  return ERR_SHUTDOWN;
}

cif_handler_t handlers[] = {
  { rom_child, "child" },
  { rom_peer, "peer" },
  { rom_parent, "parent" },
  { rom_itopackage, "instance-to-package" },
  { rom_finddevice, "finddevice" },
  { rom_getprop, "getprop" },
  { rom_getproplen, "getproplen" },
  { rom_write, "write" },
  { rom_read, "read" },
  { rom_claim, "claim" },
  { rom_shutdown, "exit" },
  { rom_shutdown, "enter" },
  { rom_shutdown, "boot" },
  { rom_shutdown, "chain" },
  { rom_milliseconds, "milliseconds" },
  { rom_callmethod, "call-method" },
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
    return ERR_NOT_ROM_CALL;
  }

  if (pc != (cif_trampoline + 4)) {
    return ERR_NOT_ROM_CALL;
  }

  cia = r->ppcGPRs[3];
  err = guest_from_x(&service_ea, CELL(cia, 0));
  ON_ERROR("service ea", err, done);

  err = guest_from_x(&in_count, CELL(cia, 1));
  ON_ERROR("in count", err, done);

  err = guest_from_x(&out_count, CELL(cia, 2));
  ON_ERROR("out count", err, done);

  service[guest_from_ex(&service, service_ea, sizeof(service) - 1, 1, true)] = '\0';

  for (i = 0; i < ARRAY_LEN(handlers); i++) {
    if (!strcmp(service, handlers[i].name)) {
      err = handlers[i].handler(cia, in_count, out_count);
      break;
    }
  }

  if (i == ARRAY_LEN(handlers)) {
    WARN("Unsupported OF call '%s' from 0x%x cia 0x%x in %u out %u",
          service, r->ppcLR, cia, in_count, out_count);
    return ERR_UNSUPPORTED;
  }

  if (err == ERR_SHUTDOWN || err == ERR_PAUSE) {
    return err;
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
