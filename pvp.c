#include "guest.h"
#include "pmem.h"
#include "rom.h"
#include "ppc-defs.h"
#include "term.h"
#include "mon.h"

#define ENTER_MON_MSG "waiting for monitor"

static bool cpu_little_endian = false;
const char *fdt_path = "pvp.dtb";

void
usage(int argc, char **argv)
{
  bool do_help = false;

  while (1) {
    int c;
    opterr = 0;
    c = getopt(argc, argv, "C:F:L");
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
  unsigned long *return_params32;
  err_t err;

  usage(argc, argv);

  err = guest_init(cpu_little_endian, MB(32));
  ON_ERROR("guest_init", err, out);

  err = term_init();
  ON_ERROR("term_init", err, out);

  err = rom_init(fdt_path);
  ON_ERROR("rom_init", err, out);

  err = mon_init();
  ON_ERROR("mon_init", err, out);
   
  LOG("Switching to guest virtual machine");
  while (1) {
    vmm_return_code_t vmm_ret;
    vmm_ret = vmm_call(kVmmExecuteVM, guest->vmm_index);
    switch (vmm_ret) {
    case kVmmReturnNull:
      break;
    case kVmmBogusContext:
      goto unhandled;
    case kVmmStopped:
      goto unhandled;
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
          ON_ERROR("guest_map", err, unhandled);
          continue;
        }
      }
      goto unhandled;
    case kVmmReturnAlignmentFault:
      goto unhandled;
    case kVmmReturnProgramException:
      goto unhandled;
    case kVmmReturnTraceException:
      goto unhandled;
    case kVmmAltivecAssist:
      goto unhandled;
    case kVmmInvalidAdSpace:
      goto unhandled;
    case kVmmReturnSystemCall:
      err = rom_call();
      if (err == ERR_SHUTDOWN) {
        goto stop;
      } else if (err != ERR_NONE) {
        ERROR(err, "rom_call");
        goto unhandled;
      }

      break;
    default:
      goto unhandled;
    }

    err = mon_check();
    if (err != ERR_NONE && err != ERR_CONTINUE) {
      goto unhandled;
    }

    continue;
  unhandled:
    if (err != ERR_NONE) {
      ERROR(err, ENTER_MON_MSG);
    } else if (vmm_ret != kVmmReturnNull) {
      VMM_ERROR(vmm_ret, ENTER_MON_MSG);
    }
    if (mon_activate() != ERR_NONE) {
      break;
    }
  }
 stop:

  LOG("Requested VM stop");
  kr = vmm_call(kVmmTearDownContext, guest->vmm_index);
  ON_MACH_ERROR("vmm_init_context", kr, out);
  VERBOSE("Virtual machine context torn down");

  term_bye();
  mon_bye();

out:
  exit(kr);
}
