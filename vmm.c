#define LOG_PFX VMM
#include "vmm.h"

// vmm_dispatch() is a PowerPC-only system call that allows us to invoke
// functions residing in the Vmm dispatch table. In general, Vmm routines
// are available to user space, but the C library (or another library) does
// not contain stubs to call them. Thus, we must go through vmm_dispatch(),
// using the index of the function to call as the first parameter in GPR3.
//
// Since vmachmon.h contains the kernel prototype of vmm_dispatch(), which
// is not what we want, we will declare our own function pointer and set
// it to the stub available in the C library.
//
vmm_dispatch_func_t vmm_call;
   
// Convenience data structure for pretty-printing Vmm features
struct VmmFeature {
  int32_t  mask;
  char    *name;
} VmmFeatures[] = {
  { kVmmFeature_LittleEndian,        "LittleEndian"        },
  { kVmmFeature_Stop,                "Stop"                },
  { kVmmFeature_ExtendedMapping,     "ExtendedMapping"     },
  { kVmmFeature_ListMapping,         "ListMapping"         },
  { kVmmFeature_FastAssist,          "FastAssist"          },
  { kVmmFeature_XA,                  "XA"                  },
  { kVmmFeature_SixtyFourBit,        "SixtyFourBit"        },
  { kVmmFeature_MultAddrSpace,       "MultAddrSpace"       },
  { kVmmFeature_GuestShadowAssist,   "GuestShadowAssist"   },
  { kVmmFeature_GlobalMappingAssist, "GlobalMappingAssist" },
  { kVmmFeature_HostShadowAssist,    "HostShadowAssist"    },
  { kVmmFeature_MultAddrSpaceAssist, "MultAddrSpaceAssist" },
  { -1, NULL },
};

const char *
vmm_return_code_to_string(vmm_return_code_t code)
{
#define _VMM_RETURN_CODE(x) case x: { \
    return #x;                        \
    break;                            \
  }

  switch(code) {
    VMM_RETURN_CODES
  default:
    return "unknown";
  }

#undef _VMM_RETURN_CODE
}

err_t
vmm_init(vmm_state_page_t **vm_state)
{
  int i;
  err_t err;
  vmm_version_t version;
  vmm_features_t features;
  kern_return_t kr;
  mach_port_t mt;
  vm_address_t vmmUStatePage = 0;
  vmm_state_page_t *vmmUState = NULL; // It's a vmm_comm_page_t too

  vmm_call = (vmm_dispatch_func_t) vmm_dispatch;
  version = vmm_call(kVmmGetVersion);
  LOG("Mac OS X virtual machine monitor (version %lu.%lu)",
      (version >> 16), (version & 0xFFFF));
   
  features = vmm_call(kVmmvGetFeatures);
  DEBUG("Vmm features:");
  for (i = 0; VmmFeatures[i].mask != -1; i++){
    DEBUG("  %-20s = %s", VmmFeatures[i].name,
          (features & VmmFeatures[i].mask) ?  "Yes" : "No");
  }

  DEBUG("Page size is %u bytes", vm_page_size);
  mt = mach_task_self();

  // VM user state
  kr = vm_allocate(mt, &vmmUStatePage, vm_page_size, VM_FLAGS_ANYWHERE);
  ON_MACH_ERROR("vm_allocate", kr, out);
  vmmUState = (vmm_state_page_t *)vmmUStatePage;

  // Initialize a new virtual machine context
  kr = vmm_call(kVmmInitContext, version, vmmUState);
  ON_MACH_ERROR("vmm_init_context", kr, out);

  *vm_state = vmmUState;
out:
  if (kr != KERN_SUCCESS) {
    return ERR_MACH;
  }
  return ERR_NONE;
}
