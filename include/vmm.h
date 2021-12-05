#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <mach/mach.h>
#include <architecture/ppc/cframe.h>
#include "types.h"
#include "defs.h"

#define ON_MACH_ERROR(msg, retval, out) \
  if (retval != KERN_SUCCESS) { MACH_ERROR(retval, msg); goto out; }

#ifndef _VMACHMON32_KLUDGE_
// We need to include xnu/osfmk/ppc/vmachmon.h, which includes several other
// kernel headers and is not really meant for inclusion in user programs.
// We perform the following kludges to include vmachmon.h to be able to
// compile this program:
//
// 1. Provide dummy forward declarations for data types that vmachmon.h
//    needs, but we will not actually use.
// 2. Copy vmachmon.h to the current directory from the kernel source tree.
// 3. Remove or comment out "#include <ppc/exception.h>" from vmachmon.h.
//
struct  savearea;             // kludge #1
typedef int ReturnHandler;    // kludge #1
typedef int pmap_t;           // kludge #1
typedef int facility_context; // kludge #1
#include "vmachmon.h"         // kludge #2
#endif

err_t pmem_init(size_t bytes);
vm_address_t pmem_base();
size_t pmem_size();
void pmem_to(gra_t dest, void *src, size_t bytes);
void pmem_from(void *dest, gra_t src, size_t bytes);

#undef ERR_DEF
