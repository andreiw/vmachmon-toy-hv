#include "guest.h"
#include "pmem.h"
#include "rom.h"
#include "ppc-defs.h"

static bool cpu_little_endian = false;
const char *fdt_path = "pvp.dtb";

void
usage(int argc, char **argv)
{
  bool do_help = false;

  while (1) {
    int c;
    opterr = 0;
    c = getopt(argc, argv, "F:L");
    if (c == -1) {
      break;
    } else if (c == '?') {
      do_help = true;
      break;
    }

    switch (c) {
    case 'F':
      fdt_path = optarg;
      break;
    case 'L':
      cpu_little_endian = true;
      break;
    }
  }

  if (!do_help) {
    return;
  }
  
  fprintf(stderr, "Usage: %s [-L] [-F fdt.dtb]\n", argv[0]);
  exit(1);
}
   
int
main(int argc, char **argv)
{
  kern_return_t kr;
  vmm_return_code_t vmm_ret;
  unsigned long *return_params32;
  err_t err;

  usage(argc, argv);

  err = guest_init(cpu_little_endian, MB(32));
  ON_ERROR("guest_init", err, out);

  err = rom_init(fdt_path);
  ON_ERROR("rom_init", err, out);
   
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
      if (rom_call() == ERR_NONE) {
        break;
      }
      goto stop;
    case kVmmReturnTraceException:
      guest_dump();
      continue;
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

  guest_dump();
   
  // Tear down the virtual machine ... that's all for now
  kr = vmm_call(kVmmTearDownContext, guest->vmm_index);
  ON_MACH_ERROR("vmm_init_context", kr, out);
  VERBOSE("Virtual machine context torn down");

out:
  exit(kr);
}
