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
#include "mmu_ranges.h"
#include "pmem.h"
#include "libfdt.h"
#include "term.h"
#include "list.h"
#include "mon.h"
#include "disk.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#define PHANDLE_MUNGE 0x10000000
#define ROOT_PHANDLE rom_get_phandle(0)
#define CELL(x, i) (x + i * sizeof(cell_t))
#define CIA_SERVICE CELL(cia, 0)
#define CIA_IN      CELL(cia, 1)
#define CIA_OUT     CELL(cia, 2)
#define CIA_ARG(x)  ({                                 \
      BUG_ON(x >= cia_in, "arg %u out of bounds", x);  \
      CELL(cia, (3 + (x)));                            \
    })
#define CIA_RET(x)  ({                                  \
      BUG_ON(x >= cia_out, "ret %u out of bounds", x);  \
      CELL(cia, (3 + cia_in + (x)));                    \
    })

typedef uint32_t cell_t;
typedef cell_t phandle_t;
typedef cell_t ihandle_t;

static void *fdt;
static int memory_node;
static struct list_head known_ihandles;
static ihandle_t mmu_ihandle;
static ihandle_t memory_ihandle;
static ranges_t guest_mem_avail_ranges;
static ranges_t guest_mem_reg_ranges;
static mmu_ranges_t rom_mmu_ranges;
static gra_t cif_trampoline;
static gra_t claim_arena_start;
static gra_t claim_arena_ptr;
static gra_t claim_arena_end;
static gra_t stack_start;
static gra_t stack_end;

static uint8_t xfer_buf[PAGE_SIZE];

struct ihandle_methods;
typedef count_t (*ihandle_write_t)(struct ihandle_methods *im,
                                   const uint8_t *b, count_t len);
typedef count_t (*ihandle_read_t)(struct ihandle_methods *im,
                                  uint8_t *b, count_t len);
typedef err_t (*ihandle_seek_t)(struct ihandle_methods *im,
                                offset_t offset);
typedef void (*ihandle_close_t)(struct ihandle_methods *im);

typedef struct ihandle_methods {
  ihandle_write_t write;
  ihandle_read_t read;
  ihandle_seek_t seek;
  ihandle_close_t close;
} ihandle_methods_t;

typedef enum ihandle_type {
  IHANDLE_BOGUS,
  IHANDLE_WRAPPED,
  IHANDLE_FILE,
  IHANDLE_DISK,
} ihandle_type_t;

typedef struct ihandle_header {
  struct list_head link;
  ihandle_type_t type;
  ihandle_t value;
  ihandle_methods_t methods;
} ihandle_header_t;

typedef struct ihandle_file {
  ihandle_header_t header;
  int fd;
  char *path;
} ihandle_file_t;

typedef struct ihandle_disk {
  ihandle_header_t header;
  disk_t *disk;
  disk_part_t part;
  offset_t current_off;
  char *path;
} ihandle_disk_t;

typedef struct {
  err_t (*handler)(gea_t cia, count_t cia_in,
                   count_t cia_out);
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

static void
rom_mon_dump_known_ihandles(void)
{
  ihandle_header_t *header;

  list_for_each_entry(header, &known_ihandles, link) {
    switch (header->type) {
    case IHANDLE_WRAPPED: {
      int node = rom_node_offset_by_ihandle(header->value);
      const char *p = "<UNKNOWN>";
      if (node > 0 &&
          fdt_get_path(fdt, node, (char *) xfer_buf,
                       sizeof(xfer_buf)) >= 0) {
        p = (const char *) xfer_buf;
      }
      mon_printf("0x%08x: wrapped (path '%s')\n",
                 header->value, p);
      break;
    }
    case IHANDLE_FILE: {
      ihandle_file_t *f = container_of(header, ihandle_file_t, header);
      mon_printf("0x%08x: file (fd %u, path = '%s')\n",
                 header->value, f->fd, f->path);
      break;
    }
    case IHANDLE_DISK: {
      ihandle_disk_t *d = container_of(header, ihandle_disk_t, header);

      mon_printf("0x%08x: disk (path = '%s')\n",
                 header->value, d->path);
      break;
    }
    case IHANDLE_BOGUS:
    default:
      mon_printf("0x%08x: <corrupt ihandle entry>\n",
                 header->value);
    }
  }
}

static ihandle_methods_t *
rom_methods_by_ihandle(ihandle_t ihandle)
{
  ihandle_header_t *header;

  list_for_each_entry(header, &known_ihandles, link) {
    if (header->value == ihandle) {
      return &header->methods;
    }
  }

  return NULL;
}

static err_t
rom_wrap_ihandle_with_methods(ihandle_t t,
                              ihandle_write_t write,
                              ihandle_read_t read,
                              ihandle_seek_t seek,
                              ihandle_close_t close)
{
  ihandle_header_t *h;

  h = malloc(sizeof(ihandle_header_t));
  if (h == NULL) {
    return ERR_NO_MEM;
  }

  h->value = t;
  h->type = IHANDLE_WRAPPED;
  h->methods.write = write;
  h->methods.read = read;
  h->methods.seek = seek;
  h->methods.close = close;
  list_add_tail(&h->link, &known_ihandles);
  return ERR_NONE;
}

static err_t
rom_file_seek(ihandle_methods_t *im,
              offset_t offset)
{
  int ret;
  ihandle_header_t *h = container_of(im, ihandle_header_t, methods);
  ihandle_file_t *f = container_of(h, ihandle_file_t, header);

  ret = lseek(f->fd, offset, SEEK_SET);
  if (ret < 0) {
    return ERR_POSIX;
  }

  return ERR_NONE;
}

static count_t
rom_file_read(ihandle_methods_t *im,
              uint8_t *s,
              count_t len)
{
  int ret;
  ihandle_header_t *h = container_of(im, ihandle_header_t, methods);
  ihandle_file_t *f = container_of(h, ihandle_file_t, header);

  ret = read(f->fd, s, len);
  if (ret < 0) {
    ret = 0;
  }

  return ret;
}

static count_t
rom_file_write(ihandle_methods_t *im,
               const uint8_t *s,
               count_t len)
{
  int ret;
  ihandle_header_t *h = container_of(im, ihandle_header_t, methods);
  ihandle_file_t *f = container_of(h, ihandle_file_t, header);

  ret = write(f->fd, s, len);
  if (ret < 0) {
    ret = 0;
  }

  return ret;
}
static void
rom_file_close(struct ihandle_methods *im)
{
  ihandle_header_t *h = container_of(im, ihandle_header_t, methods);
  ihandle_file_t *f = container_of(h, ihandle_file_t, header);

  close(f->fd);
  list_del(&h->link);
  free(f->path);
  free(f);
}

static err_t
rom_disk_seek(ihandle_methods_t *im,
              offset_t offset)
{
  ihandle_header_t *h = container_of(im, ihandle_header_t, methods);
  ihandle_disk_t *d = container_of(h, ihandle_disk_t, header);

  if (offset > d->part.length) {
    return ERR_OUT_OF_BOUNDS;
  }

  d->current_off = offset;
  return ERR_NONE;
}

static count_t
rom_disk_read(ihandle_methods_t *im,
              uint8_t *s,
              count_t len)
{
  length_t c;
  ihandle_header_t *h = container_of(im, ihandle_header_t, methods);
  ihandle_disk_t *d = container_of(h, ihandle_disk_t, header);

  c = disk_seek(d->disk, d->part.off + d->current_off);
  if (c != ERR_NONE) {
    return 0;
  }

  len = min(len, d->part.length - d->current_off);
  c = disk_in(d->disk, s, len);
  d->current_off += c;
  return c;
}

static count_t
rom_disk_write(ihandle_methods_t *im,
               const uint8_t *s,
               count_t len)
{
  length_t c;
  ihandle_header_t *h = container_of(im, ihandle_header_t, methods);
  ihandle_disk_t *d = container_of(h, ihandle_disk_t, header);

  c = disk_seek(d->disk, d->part.off + d->current_off);
  if (c != ERR_NONE) {
    return 0;
  }

  len = min(len, d->part.length - d->current_off);
  c = disk_out(d->disk, s, len);
  d->current_off += c;
  return c;
}

static void
rom_disk_close(struct ihandle_methods *im)
{
  ihandle_header_t *h = container_of(im, ihandle_header_t, methods);
  ihandle_disk_t *d = container_of(h, ihandle_disk_t, header);

  disk_close(d->disk);
  list_del(&h->link);
  free(d->path);
  free(d);
}

static err_t
rom_ihandle_from_file(const char *path,
                      const char *full_path,
                      ihandle_t *ihandle)
{
  int fd;
  ihandle_file_t *f;
  char *dup_path;

  fd = open(path, O_RDWR);
  if (fd < 0) {
    return ERR_POSIX;
  }

  f = malloc(sizeof(ihandle_file_t));
  if (f == NULL) {
    return ERR_NO_MEM;
  }

  dup_path = strdup(full_path);
  if (dup_path == NULL) {
    free(f);
    return ERR_NO_MEM;
  }

  f->header.type = IHANDLE_FILE;
  f->header.value = (ihandle_t) f;
  f->header.methods.write = rom_file_write;
  f->header.methods.read = rom_file_read;
  f->header.methods.seek = rom_file_seek;
  f->header.methods.close = rom_file_close;
  f->fd = fd;
  f->path = dup_path;
  list_add_tail(&f->header.link, &known_ihandles);
  *ihandle = f->header.value;
  return ERR_NONE;
}

static err_t
rom_ihandle_from_disk(disk_t *disk,
                      disk_part_t *part,
                      const char *full_path,
                      ihandle_t *ihandle)
{
  ihandle_disk_t *d;
  char *dup_path;

  d = malloc(sizeof(ihandle_disk_t));
  if (d == NULL) {
    return ERR_NO_MEM;
  }

  dup_path = strdup(full_path);
  if (dup_path == NULL) {
    free(d);
    return ERR_NO_MEM;
  }

  d->header.type = IHANDLE_DISK;
  d->header.value = (ihandle_t) d;
  d->header.methods.write = rom_disk_write;
  d->header.methods.read = rom_disk_read;
  d->header.methods.seek = rom_disk_seek;
  d->header.methods.close = rom_disk_close;
  d->path = dup_path;
  d->disk = disk;
  d->part = *part;
  list_add_tail(&d->header.link, &known_ihandles);
  *ihandle = d->header.value;
  return ERR_NONE;
}

static uint32_t
rom_claim_ex(uint32_t addr,
             uint32_t size,
             uint32_t align)
{
  uint32_t out;

  /*
   * This implementation doesn't comply with
   * IEEE-1275. Notably, it doesn't fail if
   * a range is already claimed.
   */

  if (align != 0) {
    claim_arena_ptr =
      ALIGN_UP(claim_arena_ptr, align);

    if (claim_arena_ptr >=
        claim_arena_end) {
      WARN("claim arena overflow");
      return -1;
    }

    out = (uint32_t) claim_arena_ptr;
    claim_arena_ptr += size;

    if (claim_arena_ptr >
        claim_arena_end) {
      WARN("claim arena overflow");
      return -1;
    }
  } else {
    if (addr + size > pmem_size()) {
      WARN("claimed [0x%x, 0x%lx) is outside pmem",
           addr, addr + size);
      return -1;
    }

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

static err_t
rom_milliseconds(gea_t cia,
                 count_t cia_in,
                 count_t cia_out)
{
  err_t err;
  cell_t ms;
  struct timeval te;
  gettimeofday(&te, NULL);

  ms = te.tv_sec * 1000 + te.tv_usec / 1000;
  err = guest_to_x(CIA_RET(0), &ms);
  ON_ERROR("out", err, done);

 done:
  return err;
}

static err_t
rom_claim(gea_t cia,
          count_t cia_in,
          count_t cia_out)
{
  err_t err;
  cell_t addr;
  cell_t size;
  cell_t align;
  cell_t out;

  err = guest_from_x(&addr, CIA_ARG(0));
  ON_ERROR("addr", err, done);

  err = guest_from_x(&size, CIA_ARG(1));
  ON_ERROR("size", err, done);

  err = guest_from_x(&align, CIA_ARG(2));
  ON_ERROR("align", err, done);

  out = rom_claim_ex(addr, size, align);

  err = guest_to_x(CIA_RET(0), &out);
  ON_ERROR("out", err, done);
 done:
  return err;
}

static err_t
rom_mem_call(gea_t cia,
             const char *call,
             count_t cia_in,
             count_t cia_out)
{
  err_t err;
  cell_t align;
  cell_t size;
  gra_t addr;
  cell_t result;

  if (strcmp("claim", call)) {
    return ERR_UNSUPPORTED;
  }

  err = guest_from_x(&align, CIA_ARG(2));
  ON_ERROR("mclaim align", err, done);

  err = guest_from_x(&size, CIA_ARG(3));
  ON_ERROR("mclaim size", err, done);

  err = guest_from_x(&addr, CIA_ARG(4));
  ON_ERROR("mclaim addr", err, done);

  result = rom_claim_ex(addr, size, align);

  err = guest_to_x(CIA_RET(1), &result);
  ON_ERROR("mclaim result", err, done);

 done:
  return err;
}

static err_t
rom_mmu_call(gea_t cia,
             const char *call,
             count_t cia_in,
             count_t cia_out)
{
  err_t err;
  gra_t phys;
  gea_t virt;
  cell_t size;
  cell_t mode;

  if (strcmp("map", call)) {
    return ERR_UNSUPPORTED;
  }

  err = guest_from_x(&mode, CIA_ARG(2));
  ON_ERROR("mmap mode", err, done);

  err = guest_from_x(&size, CIA_ARG(3));
  ON_ERROR("mmap size", err, done);

  err = guest_from_x(&virt, CIA_ARG(4));
  ON_ERROR("mmap virt", err, done);

  err = guest_from_x(&phys, CIA_ARG(5));
  ON_ERROR("mmap phys", err, done);

  if (mode == -2) {
    /*
     * Seen with veneer.exe. Weird.
     */
    WARN("treating non-standard mode %d as -1", (int) mode);
    mode = -1;
  }

  /*
   * For default mode (-1), the values for WIMGxPP
   * for memory shall be W=0, I=0, M=0, G=1, PP=10.
   * For I/O, these shall be W=1, I=1, M=0, G=1, PP=10.
   */
  BUG_ON(mode != -1, "non-default mode 0x%x not supported",
         mode);

  if (phys == virt) {
    return ERR_NONE;
  }

  if (!pmem_gra_valid(phys)) {
    return ERR_BAD_ACCESS;
  }

  BUG_ON((phys & PAGE_MASK) != 0, "bad alignment");
  BUG_ON((virt & PAGE_MASK) != 0, "bad alignment");

  size = ALIGN(size, PAGE_SIZE);
  mmu_range_add(&rom_mmu_ranges, virt, virt + size - 1,
                phys, 0);
 done:
  return err;
}

static err_t
rom_callmethod(gea_t cia,
               count_t cia_in,
               count_t cia_out)
{
  err_t err;
  gea_t method_ea;
  ihandle_t ihandle;
  char *call;
  cell_t result;

  err = guest_from_x(&method_ea, CIA_ARG(0));
  ON_ERROR("method ea", err, done);

  err = guest_from_x(&ihandle, CIA_ARG(1));
  ON_ERROR("ihandle", err, done);

  call = (char *) xfer_buf;
  call[guest_from_ex(call, method_ea, sizeof(xfer_buf) - 1, 1, true)] = '\0';

  if (ihandle == memory_ihandle) {
    err = rom_mem_call(cia, call, cia_in, cia_out);
  } else if (ihandle == mmu_ihandle) {
    err = rom_mmu_call(cia, call, cia_in, cia_out);
  } else {
    err = ERR_UNSUPPORTED;
  }

  /*
   * outer (call-method) result.
   */
  result = err == ERR_NONE ? 0 : -1;
  err = guest_to_x(CIA_RET(0), &result);
 done:
  return err;
}

static count_t
rom_console_read(ihandle_methods_t *im,
                 uint8_t *s,
                 count_t len)
{
  return term_in((char *) s, len);
}

static count_t
rom_console_write(ihandle_methods_t *im,
                  const uint8_t *s,
                  count_t len)
{
  while (len--) {
    if (*s == 0x9b) {
      term_out("\33[", 2);
    } else if (*s == 0xcd) {
      term_out("=", 1);
    } else if (*s == 0xba) {
      term_out("|", 1);
    } else if (*s == 0xbb ||
               *s == 0xc8) {
      term_out("\\", 1);
    } else if (*s == 0xbc ||
               *s == 0xc9) {
      term_out("/", 1);
    } else {
      term_out((const char *) s, 1);
    }
    s++;
  }

  return len;
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
  ihandle_t console_ihandle;
  err_t err = ERR_NONE;

  BUG_ON(pmem_size() <= MB(16), "guest RAM too small");

  INIT_LIST_HEAD(&known_ihandles);
  mmu_range_init(&rom_mmu_ranges);
  /*
   * Map everything 1:1.
   */
  mmu_range_add(&rom_mmu_ranges, 0, pmem_size() - 1, 0, 0);

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

  console_ihandle = rom_path_to_ihandle("con");
  BUG_ON(console_ihandle == -1, "console node missing from DT template");

  err = rom_wrap_ihandle_with_methods(console_ihandle,
                                      rom_console_write,
                                      rom_console_read,
                                      NULL, NULL);
  ON_ERROR("rom_wrap_ihandle_with_methods", err, done);

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

        err = guest_to(ea, &r->base, sizeof(cell_t), 1);
        ON_ERROR("base", err, done);

        len -= sizeof(cell_t);
        ea += sizeof(cell_t);

        if (len < sizeof(cell_t)) {
          break;
        }

        l = r->limit - r->base + 1;
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

        err = guest_to(ea, &b, sizeof(cell_t), 1);
        ON_ERROR("base", err, done);

        len -= sizeof(cell_t);
        ea += sizeof(cell_t);

        if (len < sizeof(cell_t)) {
          break;
        }

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
    *len_out = -1;
  } else {
    *len_out = len;
    if (data_ea != NULL) {
      err = guest_to(*data_ea, data, min(len, len_in), 1);
      ON_ERROR("data", err, done);
    }
  }

 done:
  return err;
}

static err_t
rom_getprop(gea_t cia,
            count_t cia_in,
            count_t cia_out)
{
  int node;
  err_t err;
  phandle_t phandle;
  gea_t prop_ea;
  gea_t data_ea;
  cell_t len_in;
  cell_t len_out;
  char *p = (char *) xfer_buf;

  err = guest_from_x(&phandle, CIA_ARG(0));
  ON_ERROR("phandle", err, done);

  err = guest_from_x(&prop_ea, CIA_ARG(1));
  ON_ERROR("prop ea", err, done);

  err = guest_from_x(&data_ea, CIA_ARG(2));
  ON_ERROR("data ea", err, done);

  err = guest_from_x(&len_in, CIA_ARG(3));
  ON_ERROR("len in", err, done);

  p[guest_from_ex(p, prop_ea, sizeof(xfer_buf), 1, true)] = '\0';

  node = rom_node_offset_by_phandle(phandle);
  if (node < 0) {
    WARN("looking up '%s' in unknown phandle 0x%x",
         p, phandle);
    len_out = -1;
  } else {
    err = rom_getprop_ex(node, p, len_in,
                         &data_ea, &len_out);
    if (err != ERR_NONE) {
      goto done;
    }
  }

  err = guest_to_x(CIA_RET(0), &len_out);
  ON_ERROR("len_out", err, done);

 done:
  return err;
}

static err_t
rom_getproplen(gea_t cia,
               count_t cia_in,
               count_t cia_out)
{
  int node;
  err_t err;
  phandle_t phandle;
  gea_t prop_ea;
  cell_t len_out;
  char *p = (char *) xfer_buf;

  err = guest_from_x(&phandle, CIA_ARG(0));
  ON_ERROR("phandle", err, done);

  err = guest_from_x(&prop_ea, CIA_ARG(1));
  ON_ERROR("prop ea", err, done);

  p[guest_from_ex(p, prop_ea, sizeof(xfer_buf), 1, true)] = '\0';

  node = rom_node_offset_by_phandle(phandle);
  if (node < 0) {
    err = ERR_NOT_FOUND;
    len_out = -1;
  } else {
    err = rom_getprop_ex(node, p, 0,
                         NULL, &len_out);
    if (err != ERR_NONE) {
      goto done;
    }
  }

  err = guest_to_x(CIA_RET(0), &len_out);
  ON_ERROR("len_out", err, done);

 done:
  return err;
}

static err_t
rom_child(gea_t cia,
          count_t cia_in,
          count_t cia_out)
{
  int node;
  err_t err;
  phandle_t ph;
  /*
   * 'child' always returns 0 on failure.
   */
  phandle_t ph_child = 0;

  err = guest_from_x(&ph, CIA_ARG(0));
  ON_ERROR("ph", err, done);

  node = rom_node_offset_by_phandle(ph);
  if (node < 0) {
    goto done;
  }

  node = fdt_subnode(fdt, node);
  if (node < 0) {
    goto done;
  } else {
    ph_child = rom_get_phandle(node);
  }

 done:
  err = guest_to_x(CIA_RET(0), &ph_child);
  ON_ERROR("out ph", err, done);
  return err;
}

static err_t
rom_peer(gea_t cia,
         count_t cia_in,
         count_t cia_out)
{
  int node;
  err_t err;
  phandle_t ph;
  /*
   * 'peer' always returns 0 on failure.
   */
  phandle_t ph_peer = 0;

  err = guest_from_x(&ph, CIA_ARG(0));
  ON_ERROR("ph", err, done);

  if (ph == 0) {
    ph_peer = ROOT_PHANDLE;
    goto done;
  }

  node = rom_node_offset_by_phandle(ph);
  if (node < 0) {
    goto done;
  }

  node = fdt_sibling(fdt, node);
  if (node < 0) {
    goto done;
  } else {
    ph_peer = rom_get_phandle(node);
  }

 done:
  err = guest_to_x(CIA_RET(0), &ph_peer);
  ON_ERROR("out ph", err, done);
  return err;
}

static err_t
rom_parent(gea_t cia,
           count_t cia_in,
           count_t cia_out)
{
  int node;
  err_t err;
  phandle_t ph;
  phandle_t ph_parent;

  err = guest_from_x(&ph, CIA_ARG(0));
  ON_ERROR("ph", err, done);

  if (ph == ROOT_PHANDLE) {
    ph_parent = 0;
    goto done;
  }

  node = rom_node_offset_by_phandle(ph);
  if (node < 0) {
    WARN("looking up unknown phandle 0x%x", ph);
    /*
     * Return -1. It's a nonsense value, but the
     * IEEE-1275 document doesn't cover invalid
     * phandles being passed.
     */
    ph_parent = -1;
    goto done;
  }

  node = fdt_parent_offset(fdt, node);
  BUG_ON(node < 0, "couldn't find parent");
  ph_parent = rom_get_phandle(node);

 done:
  err = guest_to_x(CIA_RET(0), &ph_parent);
  ON_ERROR("out ph", err, done);
  return err;
}

static err_t
rom_ptopath(gea_t cia,
            count_t cia_in,
            count_t cia_out)
{
  err_t err;
  int node;
  phandle_t ph;
  gea_t buf_ea;
  cell_t buf_len;
  cell_t result;

  err = guest_from_x(&ph, CIA_ARG(0));
  ON_ERROR("ph", err, access_fail);

  err = guest_from_x(&buf_ea, CIA_ARG(1));
  ON_ERROR("buf_ea", err, access_fail);

  err = guest_from_x(&buf_len, CIA_ARG(2));
  ON_ERROR("buf_len", err, access_fail);

  node = rom_node_offset_by_phandle(ph);
  if (node < 0) {
    result = -1;
    goto done;
  }

  if (fdt_get_path(fdt, node, (char *) xfer_buf,
                   sizeof(xfer_buf)) < 0) {
    WARN("could not get path for valid phandle 0x%x", ph);
    result = -1;
    goto done;
  }

  result = strlen((char *)xfer_buf);
  if (buf_len != 0) {
    buf_len = min(buf_len, result + 1);
    err = guest_to(buf_ea, xfer_buf, buf_len, 1);
    ON_ERROR("buf", err, access_fail);
  }

 done:
  err = guest_to_x(CIA_RET(0), &result);
  ON_ERROR("result", err, access_fail);
  return ERR_NONE;

 access_fail:
  return err;

}

static err_t
rom_itopath(gea_t cia,
            count_t cia_in,
            count_t cia_out)
{
  err_t err;
  int node;
  ihandle_t ih;
  gea_t buf_ea;
  cell_t buf_len;
  cell_t result;

  err = guest_from_x(&ih, CIA_ARG(0));
  ON_ERROR("ih", err, access_fail);

  err = guest_from_x(&buf_ea, CIA_ARG(1));
  ON_ERROR("buf_ea", err, access_fail);

  err = guest_from_x(&buf_len, CIA_ARG(2));
  ON_ERROR("buf_len", err, access_fail);

  /*
   * TODO: handle ihandles that are returned by open.
   */

  node = rom_node_offset_by_ihandle(ih);
  if (node < 0) {
    result = -1;
    goto done;
  }

  if (fdt_get_path(fdt, node, (char *) xfer_buf,
                   sizeof(xfer_buf)) < 0) {
    WARN("could not get path for valid ihandle 0x%x", ih);
    result = -1;
    goto done;
  }

  result = strlen((char *)xfer_buf);
  if (buf_len != 0) {
    buf_len = min(buf_len, result + 1);
    err = guest_to(buf_ea, xfer_buf, buf_len, 1);
    ON_ERROR("buf", err, access_fail);
  }

 done:
  err = guest_to_x(CIA_RET(0), &result);
  ON_ERROR("result", err, access_fail);
  return ERR_NONE;

 access_fail:
  return err;
}

static err_t
rom_itopackage(gea_t cia,
               count_t cia_in,
               count_t cia_out)
{
  err_t err;
  int node;
  ihandle_t ih;
  phandle_t ph;

  err = guest_from_x(&ih, CIA_ARG(0));
  ON_ERROR("ih", err, done);

  node = rom_node_offset_by_ihandle(ih);
  if (node < 0) {
    ph = -1;
  } else {
    ph = rom_get_phandle(node);
  }

  err = guest_to_x(CIA_RET(0), &ph);
  ON_ERROR("ph", err, done);

 done:
  return err;
}

static err_t
rom_finddevice(gea_t cia,
               count_t cia_in,
               count_t cia_out)
{
  int node;
  err_t err;
  gea_t dev_ea;
  phandle_t phandle;
  char *d = (char *) xfer_buf;

  err = guest_from_x(&dev_ea, CIA_ARG(0));
  ON_ERROR("dev ea", err, done);

  d[guest_from_ex(d, dev_ea, sizeof(xfer_buf), 1, true)] = '\0';
  node = fdt_path_offset(fdt, d);
  if (node < 0) {
    WARN("dev '%s' not found", d);
    phandle = -1;
  } else {
    phandle = rom_get_phandle(node);
  }

  err = guest_to_x(CIA_RET(0), &phandle);
  ON_ERROR("phandle", err, done);

 done:
  return err;
}

static err_t
rom_open_parse_path(char *path, ihandle_t *ihandle)
{
  char *p;
  char *f;
  int node;
  err_t err;
  disk_t *disk;
  const char *disk_path;
  disk_part_t part;
  char *dev = strdup(path);

  if (dev == NULL) {
    return ERR_NO_MEM;
  }

  f = strchr(dev, ',');
  if (f != NULL) {
    *f = '\0';
    f++;
  }

  p = strchr(dev, ':');
  if (p != NULL) {
    *p = '\0';
    p++;
  }

  if (f != NULL) {
    /*
     * Temporary, while we don't have own FS
     * handler, read from outside.
     */
    err = rom_ihandle_from_file(f, path, ihandle);
    free(dev);
    return err;
  }

  node = fdt_path_offset(fdt, dev);
  if (node < 0) {
    WARN("dev '%s' not found", dev);
    err = ERR_NOT_FOUND;
    goto done;
  }

  disk_path = fdt_getprop(fdt, node, "disk_file", NULL);
  if (disk_path == NULL) {
    WARN("dev '%s' inappropriate device", dev);
    err = ERR_UNSUPPORTED;
    goto done;
  }

  disk = disk_open(disk_path);
  if (disk == NULL) {
    return ERR_POSIX;
  }

  err = disk_find_part(disk, atoi(p), &part);
  if (err != ERR_NONE) {
    goto done;
  }

  err = rom_ihandle_from_disk(disk, &part,
                              path, ihandle);
 done:
  if (err != ERR_NONE) {
    if (disk != NULL) {
      disk_close(disk);
    }
    free(dev);
  }
  return ERR_NONE;
}

static err_t
rom_open(gea_t cia,
         count_t cia_in,
         count_t cia_out)
{
  err_t err;
  gea_t path_ea;
  /*
   * 0 on failure;
   */
  ihandle_t ihandle = 0;
  char *d = (char *) xfer_buf;

  err = guest_from_x(&path_ea, CIA_ARG(0));
  ON_ERROR("path", err, access_fail);

  d[guest_from_ex(d, path_ea, sizeof(xfer_buf), 1, true)] = '\0';
  err = rom_open_parse_path(d, &ihandle);
  if (err != ERR_NONE) {
    ERROR(err, "couldn't open '%s'", d);
  }
/* done: */

  err = guest_to_x(CIA_RET(0), &ihandle);
  ON_ERROR("ihandle", err, access_fail);

 access_fail:
  return err;
}

static err_t
rom_close(gea_t cia,
          count_t cia_in,
          count_t cia_out)
{
  err_t err;
  cell_t result;
  ihandle_t ihandle;
  ihandle_methods_t *methods;

  err = guest_from_x(&ihandle, CIA_ARG(0));
  ON_ERROR("ihandle", err, done);

  methods = rom_methods_by_ihandle(ihandle);
  if (methods == NULL ||
      methods->close == NULL) {
    result = -1;
    goto done;
  }

  methods->close(methods);

 done:
  return err;
}

static err_t
rom_seek(gea_t cia,
         count_t cia_in,
         count_t cia_out)
{
  err_t err;
  cell_t result;
  cell_t off_lo;
  cell_t off_hi;
  ihandle_t ihandle;
  ihandle_methods_t *methods;

  err = guest_from_x(&ihandle, CIA_ARG(0));
  ON_ERROR("ihandle", err, access_fail);

  err = guest_from_x(&off_hi, CIA_ARG(1));
  ON_ERROR("off_hi", err, access_fail);

  err = guest_from_x(&off_lo, CIA_ARG(2));
  ON_ERROR("off_lo", err, access_fail);

  BUG_ON(off_hi != 0, "off_hi is 0x%x", off_hi);

  methods = rom_methods_by_ihandle(ihandle);
  if (methods == NULL ||
      methods->seek == NULL) {
    result = -1;
    goto done;
  }

  if (methods->seek(methods, off_lo) == ERR_NONE) {
    result = 0;
  }

 done:
  err = guest_to_x(CIA_RET(0), &result);
  ON_ERROR("result", err, access_fail);

 access_fail:
  return err;
}


static err_t
rom_read(gea_t cia,
         count_t cia_in,
         count_t cia_out)
{
  err_t err;
  ihandle_t ihandle;
  cell_t data_ea;
  cell_t len_in;
  cell_t len_out;
  ihandle_methods_t *methods;

  err = guest_from_x(&ihandle, CIA_ARG(0));
  ON_ERROR("ihandle", err, access_fail);

  err = guest_from_x(&data_ea, CIA_ARG(1));
  ON_ERROR("data ea", err, access_fail);

  err = guest_from_x(&len_in, CIA_ARG(2));
  ON_ERROR("data ea", err, access_fail );

  methods = rom_methods_by_ihandle(ihandle);
  if (methods == NULL ||
      methods->read == NULL) {
    len_out = -1;
    goto done;
  }

  len_out = len_in;
  while (len_in != 0) {
    length_t xferred;
    length_t xfer = min(len_in, (PAGE_SIZE - (data_ea & PAGE_MASK)));
    xfer = min(xfer, sizeof(xfer_buf));

    BUG_ON(PFN(data_ea) != PFN(data_ea + xfer - 1), "bad len");
    xferred = methods->read(methods, xfer_buf, xfer);

    err = guest_to(data_ea, xfer_buf, xferred, 1);
    ON_ERROR("data", err, partial);

    len_in -= xferred;
    if (xferred != xfer) {
      err = ERR_NOT_READY;
      goto partial;
    }

    data_ea += xfer;
  };

 partial:
  if (err == ERR_BAD_ACCESS || err == ERR_NOT_READY) {
    err = ERR_NONE;
  }

  if (err == ERR_NONE) {
    len_out -= len_in;
  }

 done:
  err = guest_to_x(CIA_RET(0), &len_out);
  ON_ERROR("len_out", err, access_fail);

 access_fail:
  return err;
}

static err_t
rom_write(gea_t cia,
          count_t cia_in,
          count_t cia_out)
{
  err_t err;
  ihandle_t ihandle;
  cell_t data_ea;
  cell_t len_in;
  cell_t len_out;
  ihandle_methods_t *methods;

  err = guest_from_x(&ihandle, CIA_ARG(0));
  ON_ERROR("ihandle", err, access_fail);

  err = guest_from_x(&data_ea, CIA_ARG(1));
  ON_ERROR("data ea", err, access_fail);

  err = guest_from_x(&len_in, CIA_ARG(2));
  ON_ERROR("data ea", err, access_fail);

  methods = rom_methods_by_ihandle(ihandle);
  if (methods == NULL ||
      methods->read == NULL) {
    len_out = -1;
    goto done;
  }

  len_out = len_in;
  while (len_in != 0) {
    length_t xferred;
    length_t xfer = min(len_in, (PAGE_SIZE - (data_ea & PAGE_MASK)));
    xfer = min(xfer, sizeof(xfer_buf));

    err = guest_from(xfer_buf, data_ea, xfer, 1);
    ON_ERROR("data", err, partial);

    BUG_ON(PFN(data_ea) != PFN(data_ea + xfer - 1), "bad len");
    xferred = methods->write(methods, xfer_buf, xfer);

    len_in -= xferred;
    if (xferred != xfer) {
      err = ERR_NOT_READY;
      goto partial;
    }

    data_ea += xfer;
  };

 partial:
  if (err == ERR_BAD_ACCESS || err == ERR_NOT_READY) {
    err = ERR_NONE;
  }

  if (err == ERR_NONE) {
    len_out -= len_in;
  }

 done:
  err = guest_to_x(CIA_RET(0), &len_out);
  ON_ERROR("len_out", err, done);

 access_fail:
  return err;
}

static err_t
rom_shutdown(gea_t cia,
             count_t cia_in,
             count_t cia_out)
{
  return ERR_SHUTDOWN;
}

cif_handler_t handlers[] = {
  { rom_child, "child" },
  { rom_peer, "peer" },
  { rom_parent, "parent" },
  { rom_itopackage, "instance-to-package" },
  { rom_itopath, "instance-to-path" },
  { rom_finddevice, "finddevice" },
  { rom_getprop, "getprop" },
  { rom_getproplen, "getproplen" },
  { rom_open, "open" },
  { rom_seek, "seek" },
  { rom_close, "close" },
  { rom_write, "write" },
  { rom_read, "read" },
  { rom_claim, "claim" },
  { rom_shutdown, "exit" },
  { rom_shutdown, "enter" },
  { rom_shutdown, "boot" },
  { rom_shutdown, "chain" },
  { rom_milliseconds, "milliseconds" },
  { rom_callmethod, "call-method" },
  { rom_ptopath, "package-to-path" }
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
  err = guest_from_x(&service_ea, CIA_SERVICE);
  ON_ERROR("service ea", err, done);

  err = guest_from_x(&in_count, CIA_IN);
  ON_ERROR("in count", err, done);

  err = guest_from_x(&out_count, CIA_OUT);
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

err_t
rom_fault(gea_t gea,
          gra_t *gra,
          guest_fault_t flags)
{
  mmu_range_t *m = mmu_range_find(&rom_mmu_ranges, gea);

  if (m == NULL) {
    return ERR_NOT_FOUND;
  }

  /*
   * guest_fault_t is unused here as all ROM
   * mappings seen today are done with default
   * mode, so nothing to check.
   *
   * See rom_mmu_call for details.
   */
  *gra = gea - m->base + m->ra;
  return ERR_NONE;
}

void
rom_mon_dump(void)
{
  mon_printf("claim arena:\n");
  mon_printf("  start = 0x%08x\n", claim_arena_start);
  mon_printf("    ptr = 0x%08x\n", claim_arena_ptr);
  mon_printf("    end = 0x%08x\n", claim_arena_end);
  mon_printf("stack:\n");
  mon_printf("  start = 0x%08x\n", stack_start);
  mon_printf("    end = 0x%08x\n", stack_end);
  mon_printf("guest_mem_avail_ranges:\n");
  range_dump(&guest_mem_avail_ranges);
  mon_printf("guest_mem_reg_ranges:\n");
  range_dump(&guest_mem_reg_ranges);
  mon_printf("rom_mmu_ranges:\n");
  mmu_range_dump(&rom_mmu_ranges);
  mon_printf("known_ihandles:\n");
  rom_mon_dump_known_ihandles();
}
