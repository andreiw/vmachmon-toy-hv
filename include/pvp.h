#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <mach/mach.h>
#include "types.h"
#include "defs.h"
#include "log.h"

err_t pmem_init(size_t bytes);
vm_address_t pmem_base();
size_t pmem_size();
void pmem_to(gra_t dest, void *src, size_t bytes);
void pmem_from(void *dest, gra_t src, size_t bytes);

#define ON_MACH_ERROR(msg, retval, out) \
  if (retval != KERN_SUCCESS) { MACH_ERROR(retval, msg); goto out; }


#undef ERR_DEF
