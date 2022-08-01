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
  uint32_t hvcall = 0x0f040000;

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
  guest->regs->ppcGPRs[1] = (uint32_t)PAGE2SP(guestStackAddress);
  guest->regs->ppcGPRs[5] = (uint32_t) 0x4; // CIF entry
  pmem_to(0x4, &hvcall, sizeof(hvcall));

  // Temp for CIF
  vmm_ret = vmm_call(kVmmMapPage, guest->vmm_index, pmem_base() +
                     0, 0, VM_PROT_ALL);
  if (vmm_ret != kVmmReturnNull) {
    VMM_ERROR(vmm_ret, "kVmmMapPage CIF");
  }

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
    return;
  }
  
  fprintf(stderr, "Usage: %s [-L]\n", argv[0]);
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

    usage(argc, argv);

    err = guest_init(cpu_little_endian, MB(32));
    if (err != ERR_NONE) {
      ERROR(err, "guest_init");
      goto out;
    }

    initGuestText_ROM();
   
    LOG("Switching to guest virtual machine");
    while (1) {
      vmm_ret = vmm_call(kVmmExecuteVM, guest->vmm_index);

      if (vmm_ret == kVmmReturnProgramException &&
          guest->regs->ppcPC == 0x4) {
        gra_t cia = guest->regs->ppcGPRs[3];
        gra_t cif_0;
        pmem_from(&cif_0, cia, 4);
        char *name = (char *) pmem_base() + cif_0;

        WARN("CIF call from 0%lx: %s\n", guest->regs->ppcLR, name);

        if (!strcmp("finddevice", name)) {
          gra_t p;
          pmem_from(&p, cia + 3 * 4, 4);
          WARN("-> %s\n", (char *) pmem_base() + p);
        }

        guest->regs->ppcPC = guest->regs->ppcLR;
        continue;
      }

      if (vmm_ret != kVmmReturnNull) {
        break;
      }
    }

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
