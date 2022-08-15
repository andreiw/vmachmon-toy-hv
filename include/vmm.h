#pragma once

#include "pvp.h"
#include <architecture/ppc/cframe.h>

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

err_t vmm_init(void);
err_t vmm_init_vm(vmm_state_page_t **vm_state);
const char *vmm_return_code_to_string(vmm_return_code_t code);
typedef int (* vmm_dispatch_func_t)(int, ...);
extern vmm_dispatch_func_t vmm_call;

