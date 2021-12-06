#include "pvp.h"
#include "log.h"
#include "ppc-defs.h"
   
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
typedef int (* vmm_dispatch_func_t)(int, ...);
vmm_dispatch_func_t my_vmm_dispatch;
   
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

char *
vmm_return_code_to_string(vmm_return_code_t code)
{
#define _VMM_RETURN_CODE(x) case x: {		\
    return #x;					\
    break;					\
  }

  switch(code) {
    VMM_RETURN_CODES
  default:
    return "unknown";
  }

#undef _VMM_RETURN_CODE
}
   
// Function to initialize a memory buffer with some machine code
void
initGuestText_Dummy(gra_t gra,
                    gea_t gea,
                    vmm_regs32_t *ppcRegs32)
{
  uint32_t text[6];

  // We will execute a stream of a few instructions in the virtual machine
  // through the Vmm (that is, us). I0 and I1 will load integer values into
  // registers GPR10 and GPR11. I3 will be an illegal instruction. I2 will
  // jump over I3 by unconditionally branching to I4, which will sum GPR10
  // and GPR11, placing their sum in GPR12.
  //
  // We will allow I5 to either be illegal, in which case control will
  // return to the Vmm, or, be a branch to itself: an infinite
  // loop. One Infinite Loop.
  //
  I_addi_d_form   *I0;
  I_addi_d_form   *I1;
  I_branch_i_form *I2;
  // I3 is illegal
  I_add_xo_form   *I4;
  I_branch_i_form *I5;

  // Guest will run the following instructions
  I0 = (I_addi_d_form   *)(text + 0);
  I1 = (I_addi_d_form   *)(text + 1);
  I2 = (I_branch_i_form *)(text + 2);
  text[3] = 0xdeadbeef; // illegal
  I4 = (I_add_xo_form   *)(text + 4);

  // Possibly overridden by an illegal instruction below
  I5 = (I_branch_i_form *)(text + 5);

  // Use an illegal instruction to be the last inserted instruction (I5)
  // in the guest's instruction stream
  text[5] = 0xfeedface;

  // Fill the instruction templates

  // addi r10,0,4     ; I0
  I0->OP = 14;
  I0->RT = 10;
  I0->RA = 0;
  I0->SI = 4; // load the value '4' in r10

  // addi r11,0,5     ; I1
  I1->OP = 14;
  I1->RT = 11;
  I1->RA = 0;
  I1->SI = 5; // load the value '5' in r11

  // ba               ; I2
  // We want to branch to the absolute address of the 5th instruction,
  // where the first instruction is at guestTextAddress. Note the shifting.
  //
  I2->OP = 18;
  I2->LI = (gea + (4 * 4)) >> 2;
  I2->AA = 1;
  I2->LK = 0;

  // I3 is illegal; already populated in the stream

  // add  r12,r10,r11 ; I4
  I4->OP = 31;
  I4->RT = 12;
  I4->RA = 10;
  I4->RB = 11;
  I4->OE = 0;
  I4->XO = 266;
  I4->Rc = 0;

  // I5 is illegal or an infinite loop; already populated in the stream

  LOG("Fabricated instructions for executing "
         "in the guest virtual machine");
  pmem_to(gra, text, sizeof(text));
}
   
// Function to initialize a memory buffer with some machine code
void
initGuestText_Factorial(gra_t         gra,
                        gea_t         gea,
                        vmm_regs32_t *ppcRegs32)
{
    // Machine code for the following function:
    //
    // int 
    // factorial(int n)
    // {
    //     if (n <= 0)
    //         return 1;
    //     else
    //         return n * factorial(n - 1);
    // }
    //
    // You can obtain this from the function's C source using a command-line
    // sequence like the following:
    //
    // $ gcc -static -c factorial.c
    // $ otool -tX factorial.o
    // ...
    //
    u_int32_t factorial_ppc32[] = {
        0x7c0802a6, 0xbfc1fff8, 0x90010008, 0x9421ffa0,
        0x7c3e0b78, 0x907e0078, 0x801e0078, 0x2f800000,
        0x419d0010, 0x38000001, 0x901e0040, 0x48000024,
        0x805e0078, 0x3802ffff, 0x7c030378, 0x4bffffc5,
        0x7c621b78, 0x801e0078, 0x7c0201d6, 0x901e0040,
        0x807e0040, 0x80210000, 0x80010008, 0x7c0803a6,
        0xbbc1fff8, 0x4e800020,
    };

    pmem_to(gra, factorial_ppc32, sizeof(factorial_ppc32)/sizeof(u_int8_t));
   
    // This demo takes an argument in GPR3: the number whose factorial is to
    // be computed. The result is returned in GPR3.
    //
    ppcRegs32->ppcGPRs[3] = 10; // factorial(10)
   
    // Set the LR to the end of the text in the guest's virtual address space.
    // Our demo will only use the LR for returning to the Vmm by placing an
    // illegal instruction's address in it.
    //
    ppcRegs32->ppcLR = gea + vm_page_size - 4;
   
    LOG("Injected factorial instructions for executing "
           "in the guest virtual machine");
}
   
// Some modularity... these are the demos our program supports
typedef void (* initGuestText_Func)(gra_t, gea_t, vmm_regs32_t *);
typedef struct {
    const char         *name;
    initGuestText_Func  textfiller;
} Demo;
   
Demo SupportedDemos[] = {
    {
        "executes a few hand-crafted instructions in a VM",
        initGuestText_Dummy,
    },
    {
        "executes a recursive factorial function in a VM",
        initGuestText_Factorial,
    },
};
#define MAX_DEMO_ID (sizeof(SupportedDemos)/sizeof(Demo))
   
static int demo_id = -1;
static bool cpu_little_endian = false;

void
usage(int argc, char **argv)
{
  int i;
  bool do_help = false;

  while (1) {
    int c;
    opterr = 0;
    c = getopt(argc, argv, "L");
    if (c == -1) {
      break;
    } else if (c == '?') {
      do_help = true;
      break;
    }

    switch (c) {
    case 'L':
      cpu_little_endian = true;
      break;
    }
  }

  if (!do_help) {
    if (optind < argc) {
      demo_id = atoi(argv[optind]);
      if ((demo_id >= 0) && (demo_id < MAX_DEMO_ID)) {
        return;
      }
    }
  }
  
  fprintf(stderr, "Usage: %s [-L] <demo ID>\nSupported demos:\n"
          "  ID\tDescription\n", argv[0]);
  for (i = 0; i < MAX_DEMO_ID; i++){
    fprintf(stderr, "  %d\t%s\n", i, SupportedDemos[i].name);
  }
  exit(1);
}
   
int
main(int argc, char **argv)
{
    int i, j;

    vmm_return_code_t   vmm_ret;
    kern_return_t       kr;
    mach_port_t         myTask;
    unsigned long      *return_params32;
    vmm_features_t      features;
    vmm_regs32_t       *ppcRegs32;
    vmm_version_t       version;
    vmm_thread_index_t  vmmIndex;             // The VM's index
    vm_address_t        vmmUStatePage = 0;    // Page for VM's user state
    vmm_state_page_t   *vmmUState;            // It's a vmm_comm_page_t too
    vm_address_t        guestTextAddress = 0;
    vm_address_t        guestStackAddress = 0;
    err_t               err;
   
    my_vmm_dispatch = (vmm_dispatch_func_t)vmm_dispatch;
    
    // Ensure that the user chose a demo
    usage(argc, argv);
   
    // Get Vmm version implemented by this kernel
    version = my_vmm_dispatch(kVmmGetVersion);
    LOG("Mac OS X virtual machine monitor (version %lu.%lu)",
           (version >> 16), (version & 0xFFFF));
   
    // Get features supported by this Vmm implementation
    features = my_vmm_dispatch(kVmmvGetFeatures);
    DEBUG("Vmm features:");
    for (i = 0; VmmFeatures[i].mask != -1; i++){
      DEBUG("  %-20s = %s", VmmFeatures[i].name,
            (features & VmmFeatures[i].mask) ?  "Yes" : "No");
    }
   
    DEBUG("Page size is %u bytes", vm_page_size);
   
    myTask = mach_task_self(); // to save some characters (sure)
 
    // VM user state
    kr = vm_allocate(myTask, &vmmUStatePage, vm_page_size, VM_FLAGS_ANYWHERE);
    ON_MACH_ERROR("vm_allocate", kr, out);
    LOG("Allocated page-aligned memory for virtual machine user state");
    vmmUState = (vmm_state_page_t *)vmmUStatePage;

    err = pmem_init(vm_page_size * 2);
    if (err != ERR_NONE) {
      goto out;
    }
       
    // We will lay out the text and stack pages adjacent to one another in
    // the guest's virtual address space.
    //
    // Virtual addresses increase -->
    // 0              4K             8K             12K
    // +--------------------------------------------+
    // | __PAGEZERO   |  GUEST_TEXT  | GUEST_STACK  |
    // +--------------------------------------------+
    //
    // We put the text page at virtual offset vm_page_size and the stack
    // page at virtual offset (2 * vm_page_size).
    //
   
    guestTextAddress = vm_page_size;
    guestStackAddress = 2 * vm_page_size;
   
    // Initialize a new virtual machine context
    kr = my_vmm_dispatch(kVmmInitContext, version, vmmUState);
    ON_MACH_ERROR("vmm_init_context", kr, out);
   
    // Fetch the index returned by vmm_init_context()
    vmmIndex = vmmUState->thread_index;
    LOG("New virtual machine context initialized, index = %lu", vmmIndex);

    kr = my_vmm_dispatch(kVmmActivateXA, vmmIndex, vmmGSA);
    if (kr != KERN_SUCCESS) {
      mach_error("*** kVmmActivateXA not enabling GSA:", kr);
    }

    // Set a convenience pointer to the VM's registers
    ppcRegs32 = &(vmmUState->vmm_proc_state.ppcRegs.ppcRegs32);
   
    // Set the program counter to the beginning of the text in the guest's
    // virtual address space
    ppcRegs32->ppcPC = guestTextAddress;
    LOG("Guest virtual machine PC set to %p", (void *)guestTextAddress);

    if (cpu_little_endian) {
      ppcRegs32->ppcMSR |= MSR_LE;
    }
    
    // Set the stack pointer (GPR1), taking the Red Zone into account
    #define PAGE2SP(x) ((void *)((x) + vm_page_size - C_RED_ZONE))
    ppcRegs32->ppcGPRs[1] = (u_int32_t)PAGE2SP(guestStackAddress); // 32-bit
    LOG("Guest virtual machine SP set to %p", PAGE2SP(guestStackAddress));
   
    // Map the stack page into the guest's address space
    kr = my_vmm_dispatch(kVmmMapPage, vmmIndex, pmem_base() +
                         vm_page_size, guestStackAddress, VM_PROT_ALL);
    LOG("Mapping guest stack page");
   
    // Call the chosen demo's instruction populator
    (SupportedDemos[demo_id].textfiller)(0, guestTextAddress, ppcRegs32);
   
    // Finally, map the text page into the guest's address space, and set the
    // VM running
    //

    LOG("Mapping guest text page and switching to guest virtual machine");
    vmm_ret = my_vmm_dispatch(kVmmMapExecute, vmmIndex, pmem_base() + 0,
                              guestTextAddress, VM_PROT_ALL);
   
    // Our demo ensures that the last instruction in the guest's text is
    // either an infinite loop or illegal. The monitor will "hang" in the case
    // of an infinite loop. It will have to be interupted (^C) to gain control.
    // In the case of an illegal instruction, the monitor will gain control at
    // this point, and the following code will be executed. Depending on the
    // exact illegal instruction, Mach's error messages may be different.
    //
    if (vmm_ret != kVmmReturnNull) {
      VMM_ERROR(vmm_ret, "*** vmm_map_execute");
    }

    LOG("Returned to vmm");
    LOG("Processor state:");
   
    LOG("  Distance from origin = %lu instructions",
           (ppcRegs32->ppcPC - vm_page_size) >> 2);
   
    LOG("  PC                   = %p (%lu)",
           (void *)ppcRegs32->ppcPC, ppcRegs32->ppcPC);
   
    LOG("  Instruction at PC    = %#08x",
           ((u_int32_t *)(pmem_base()))[(ppcRegs32->ppcPC - vm_page_size) >> 2]);
   
    LOG("  CR                   = %#08lx"
           "                         ", ppcRegs32->ppcCR);

    LOG("  LR                   = %#08lx (%lu)",
           ppcRegs32->ppcLR, ppcRegs32->ppcLR);
   
    LOG("  MSR                  = %#08lx"
           "                         ", ppcRegs32->ppcMSR);

    LOG("  return_code          = %#08lx (%s)",
           vmmUState->return_code, vmm_return_code_to_string(vmmUState->return_code));
   
    return_params32 = vmmUState->vmmRet.vmmrp32.return_params;
   
    for (i = 0; i < 4; i++)
        LOG("  return_params32[%d]   = 0x%08lx (%lu)", i,
               return_params32[i], return_params32[i]);
   
    LOG("  GPRs:");
    for (j = 0; j < 16; j++) {
      LOG("r%-2d = %#08lx r%-2d = %#08lx",
          j * 2, ppcRegs32->ppcGPRs[j * 2],
          j * 2 + 1, ppcRegs32->ppcGPRs[j * 2 + 1]);
    }
   
    // Tear down the virtual machine ... that's all for now
    kr = my_vmm_dispatch(kVmmTearDownContext, vmmIndex);
    ON_MACH_ERROR("vmm_init_context", kr, out);
    VERBOSE("Virtual machine context torn down");

out:
    exit(kr);
}
