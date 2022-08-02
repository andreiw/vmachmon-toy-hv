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

  if (read(fd, (void *) pmem_ha(guestTextAddress), st.st_size) == -1) {
    POSIX_ERROR(errno, "read");
  }

  guest->regs->ppcPC = guestTextAddress;
  guest->regs->ppcGPRs[1] = (uint32_t)PAGE2SP(guestStackAddress);
  guest->regs->ppcGPRs[5] = (uint32_t) 0x4; // CIF entry
  pmem_to(0x4, &hvcall, sizeof(hvcall));
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

    switch (vmm_ret) {
    case kVmmReturnNull:
      break;
    case kVmmBogusContext:
      goto stop;
    case kVmmStopped:
      goto stop;;
    case kVmmReturnDataPageFault:
    case kVmmReturnInstrPageFault:
      {
        uint32_t address;
        uint32_t dsisr;
        ha_t host_address;

        return_params32 = guest->vmm->vmmRet.vmmrp32.return_params;
        address = return_params32[0] & ~PAGE_MASK;
        dsisr = return_params32[1];

        if (guest_mmu_allow_ra() &&
            pmem_gra_valid(address) &&
            (dsisr & DSISR_NOT_PRESENT) != 0) {
          err = guest_map(pmem_ha(address), address);
          ON_ERROR("guest_map", err, stop);
          continue;
        }
      }
      goto stop;
    case kVmmReturnAlignmentFault:
      goto stop;
    case kVmmReturnProgramException:
      if (guest->regs->ppcPC == 0x4) {
        gra_t cia;
        gea_t cif_0;
        gra_t cif_0_ra;

        err = guest_backmap(guest->regs->ppcGPRs[3], &cia);
        ON_ERROR("guest_backmap", err, stop);
        pmem_from(&cif_0, cia, 4);
        err = guest_backmap(cif_0, &cif_0_ra);
        ON_ERROR("guest_backmap", err, stop);

        char *name = (char *) pmem_ha(cif_0_ra);

        WARN("CIF call from 0%lx: %s", guest->regs->ppcLR, name);

        if (!strcmp("finddevice", name)) {
          gra_t p;
          pmem_from(&p, cia + 3 * 4, 4);
          WARN("-> %s", (char *) pmem_ha(p));
        }

        guest->regs->ppcPC = guest->regs->ppcLR;
        break;
      }
      goto stop;
    case kVmmReturnTraceException:
      goto stop;
    case kVmmAltivecAssist:
      goto stop;
    case kVmmInvalidAdSpace:
      goto stop;
    default:
      goto stop;
    }

    continue;
  stop:
    break;
  }

  LOG("Processor state:");

  LOG("  PC                   = %p (%lu)",
      (void *)guest->regs->ppcPC, guest->regs->ppcPC);
   
  LOG("  Instruction at PC    = %#08x",
      *(u_int32_t *)pmem_ha(guest->regs->ppcPC));
   
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
