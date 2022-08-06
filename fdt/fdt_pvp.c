#include "libfdt_env.h"

#include <fdt.h>
#include <libfdt.h>

#include "libfdt_internal.h"

int fdt_subnode(const void *fdt, int offset)
{
  int depth;

  FDT_CHECK_HEADER(fdt);

  for (depth = 0;
       (offset >= 0) && (depth >= 0);
       offset = fdt_next_node(fdt, offset, &depth)) {
    if (depth == 1) {
      return offset;
    }
  }

  if (depth < 0) {
    return -FDT_ERR_NOTFOUND;
  }

  return offset; /* error */
}

int fdt_sibling(const void *fdt, int offset)
{
  int depth;

  FDT_CHECK_HEADER(fdt);

  for (depth = 1, offset = fdt_next_node(fdt, offset, &depth);
       (offset >= 0) && (depth >= 1);
       offset = fdt_next_node(fdt, offset, &depth)) {
    if (depth == 1) {
      return offset;
    }
  }

  if (depth < 0) {
    return -FDT_ERR_NOTFOUND;
  }

  return offset; /* error */
}

int fdt_node_check_dtype(const void *fdt, int nodeoffset,
                         const char *dtype)
{
  const void *prop;
  int len;

  prop = fdt_getprop(fdt, nodeoffset, "device_type", &len);
  if (!prop) {
    return len;
  }

  if (_fdt_stringlist_contains(prop, len, dtype)) {
    return 0;
  } else {
    return 1;
  }
}

int fdt_node_offset_by_dtype(const void *fdt, int startoffset,
                             const char *dtype)
{
  int offset, err;

  FDT_CHECK_HEADER(fdt);

  /* FIXME: The algorithm here is pretty horrible: we scan each
   * property of a node in fdt_node_check_dtype(), then if
   * that didn't find what we want, we scan over them again
   * making our way to the next node.  Still it's the easiest to
   * implement approach; performance can come later. */
  for (offset = fdt_next_node(fdt, startoffset, NULL);
       offset >= 0;
       offset = fdt_next_node(fdt, offset, NULL)) {
    err = fdt_node_check_dtype(fdt, offset, dtype);
    if ((err < 0) && (err != -FDT_ERR_NOTFOUND)) {
      return err;
    } else if (err == 0) {
      return offset;
    }
  }

  return offset; /* error from fdt_next_node() */
}
