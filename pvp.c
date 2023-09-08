#include "guest.h"
#include "pmem.h"
#include "rom.h"
#include "ppc-defs.h"
#include "term.h"
#include "mon.h"
#include "disk.h"

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
   
  LOG("Switching to guest virtual machine TI 0x%x",
      guest->vmm->thread_index);
  while (1) {
    vmm_return_code_t vmm_ret;

    vmm_ret = vmm_call(kVmmExecuteVM, guest->vmm->thread_index);
    switch (vmm_ret) {
    case kVmmReturnNull:
      break;
    case kVmmBogusContext:
      goto unhandled;
    case kVmmStopped:
      goto unhandled;
    case kVmmReturnDataPageFault:
    case kVmmReturnInstrPageFault:
      err = guest_fault(vmm_ret == kVmmReturnInstrPageFault);
      if (err != ERR_NONE) {
        goto unhandled;
      }
      break;
    case kVmmReturnAlignmentFault:
      goto unhandled;
    case kVmmReturnProgramException:
      err = guest_emulate();
      if (err != ERR_NONE) {
        goto unhandled;
      }
      break;
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

    err = mon_trace();
    if (err == ERR_SHUTDOWN) {
      goto stop;
    }
    if (err != ERR_NONE) {
      goto unhandled;
    }

    err = mon_check();
    if (err == ERR_SHUTDOWN) {
      goto stop;
    }
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
  guest_bye();
  disk_bye();
  term_bye();
  mon_bye();

out:
  if (err == ERR_NONE) {
    return 0;
  }

  return 1;
}
