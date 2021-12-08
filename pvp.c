#include "guest.h"
#include "ppc-defs.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define PAGE2SP(x) ((void *)((x) + vm_page_size - C_RED_ZONE))

void
initGuestText_ROM(void)
{
  int fd;
  gea_t guestTextAddress = 0;
  gea_t guestStackAddress = 0;
  vmm_return_code_t vmm_ret;
  struct stat st;
  size_t page_bytes;

  guestTextAddress = 0x3e0000;
  guestStackAddress = 2 * vm_page_size;

  fd = open("iquik.b", O_RDONLY);
  if (fd == -1) {
    POSIX_ERROR(errno, "open");
  }

  if (fstat(fd, &st) == -1) {
    POSIX_ERROR(errno, "stat");
  }

  if (read(fd, (void *) (pmem_base() + guestTextAddress), st.st_size) == -1) {
    POSIX_ERROR(errno, "read");
  }

  guest->regs->ppcPC = guestTextAddress;

  // Set the stack pointer (GPR1), taking the Red Zone into account
  guest->regs->ppcGPRs[1] = (u_int32_t)PAGE2SP(guestStackAddress); // 32-bit

  vmm_ret = vmm_call(kVmmMapPage, guest->vmm_index, pmem_base() +
		guestStackAddress, guestStackAddress, VM_PROT_ALL);
  if (vmm_ret != kVmmReturnNull) {
    VMM_ERROR(vmm_ret, "kVmmMapPage stack");
  }

  page_bytes = ALIGN_UP(st.st_size, vm_page_size);
  while (page_bytes != 0) {
    vmm_ret = vmm_call(kVmmMapPage, guest->vmm_index, pmem_base() +
		       guestTextAddress, guestTextAddress, VM_PROT_ALL);
    if (vmm_ret != kVmmReturnNull) {
      VMM_ERROR(vmm_ret, "kVmmMapPage text");
    }

    guestTextAddress += vm_page_size;
    page_bytes -= vm_page_size;
  }
}

   
// Function to initialize a memory buffer with some machine code
void
initGuestText_Dummy(void)
{
  gea_t guestTextAddress = 0;
  gea_t guestStackAddress = 0;
  vmm_return_code_t vmm_ret;
  uint32_t text[6];

  guestTextAddress = vm_page_size;
  guestStackAddress = 2 * vm_page_size;

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
  I2->LI = (guestTextAddress + (4 * 4)) >> 2;
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

  pmem_to(guestTextAddress, text, sizeof(text));

  guest->regs->ppcPC = guestTextAddress;

  // Set the stack pointer (GPR1), taking the Red Zone into account
  guest->regs->ppcGPRs[1] = (u_int32_t)PAGE2SP(guestStackAddress); // 32-bit

  vmm_ret = vmm_call(kVmmMapPage, guest->vmm_index, pmem_base() +
		guestStackAddress, guestStackAddress, VM_PROT_ALL);
  if (vmm_ret != kVmmReturnNull) {
    VMM_ERROR(vmm_ret, "kVmmMapPage stack");
  }

  vmm_ret = vmm_call(kVmmMapPage, guest->vmm_index, pmem_base() +
		     guestTextAddress, guestTextAddress, VM_PROT_ALL);
  if (vmm_ret != kVmmReturnNull) {
    VMM_ERROR(vmm_ret, "kVmmMapPage text");
  }

  LOG("Fabricated instructions for executing "
         "in the guest virtual machine");
}
   
// Function to initialize a memory buffer with some machine code
void
initGuestText_Factorial(void)
{
  vm_address_t      guestTextAddress = 0;
  vm_address_t      guestStackAddress = 0;
  vmm_return_code_t vmm_ret;

  guestTextAddress = vm_page_size;
  guestStackAddress = 2 * vm_page_size;

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

  pmem_to(guestTextAddress, factorial_ppc32, sizeof(factorial_ppc32)/sizeof(u_int8_t));
   
  // This demo takes an argument in GPR3: the number whose factorial is to
  // be computed. The result is returned in GPR3.
  //
  guest->regs->ppcGPRs[3] = 10; // factorial(10)
   
  // Set the LR to the end of the text in the guest's virtual address space.
  // Our demo will only use the LR for returning to the Vmm by placing an
  // illegal instruction's address in it.
  //
  guest->regs->ppcLR = guestTextAddress + vm_page_size - 4;
  guest->regs->ppcPC = guestTextAddress;

  // Set the stack pointer (GPR1), taking the Red Zone into account
  guest->regs->ppcGPRs[1] = (u_int32_t)PAGE2SP(guestStackAddress); // 32-bit

  vmm_ret = vmm_call(kVmmMapPage, guest->vmm_index, pmem_base() +
		guestStackAddress, guestStackAddress, VM_PROT_ALL);
  if (vmm_ret != kVmmReturnNull) {
    VMM_ERROR(vmm_ret, "kVmmMapPage stack");
  }

  vmm_ret = vmm_call(kVmmMapPage, guest->vmm_index, pmem_base() +
		     guestTextAddress, guestTextAddress, VM_PROT_ALL);
  if (vmm_ret != kVmmReturnNull) {
    VMM_ERROR(vmm_ret, "kVmmMapPage text");
  }

  LOG("Injected factorial instructions for executing "
      "in the guest virtual machine");
}
   
// Some modularity... these are the demos our program supports
typedef void (* initGuestText_Func)(void);
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
    {
      "load from ROM file",
      initGuestText_ROM,
    }
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
    unsigned long      *return_params32;
    err_t               err;
    
    // Ensure that the user chose a demo
    usage(argc, argv);

    err = guest_init(cpu_little_endian, MB(32));
    if (err != ERR_NONE) {
      ERROR(err, "guest_init");
      goto out;
    }

    // Call the chosen demo's instruction populator
    (SupportedDemos[demo_id].textfiller)();
   
    LOG("Mapping guest text page and switching to guest virtual machine");
    vmm_ret = vmm_call(kVmmExecuteVM, guest->vmm_index);
   
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

    LOG("  PC                   = %p (%lu)",
           (void *)guest->regs->ppcPC, guest->regs->ppcPC);
   
    LOG("  Instruction at PC    = %#08x",
           ((u_int32_t *)(pmem_base()))[(guest->regs->ppcPC) >> 2]);
   
    LOG("  CR                   = %#08lx"
           "                         ", guest->regs->ppcCR);

    LOG("  LR                   = %#08lx (%lu)",
           guest->regs->ppcLR, guest->regs->ppcLR);
   
    LOG("  MSR                  = %#08lx"
           "                         ", guest->regs->ppcMSR);

    LOG("  return_code          = %#08lx (%s)",
           guest->vmm->return_code, vmm_return_code_to_string(guest->vmm->return_code));
   
    return_params32 = guest->vmm->vmmRet.vmmrp32.return_params;
   
    for (i = 0; i < 4; i++)
        LOG("  return_params32[%d]   = 0x%08lx (%lu)", i,
               return_params32[i], return_params32[i]);
   
    LOG("  GPRs:");
    for (j = 0; j < 16; j++) {
      LOG("r%-2d = %#08lx r%-2d = %#08lx",
          j * 2, guest->regs->ppcGPRs[j * 2],
          j * 2 + 1, guest->regs->ppcGPRs[j * 2 + 1]);
    }
   
    // Tear down the virtual machine ... that's all for now
    kr = vmm_call(kVmmTearDownContext, guest->vmm_index);
    ON_MACH_ERROR("vmm_init_context", kr, out);
    VERBOSE("Virtual machine context torn down");

out:
    exit(kr);
}
