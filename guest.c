#include "guest.h"
#include "pmem.h"
#include "vmm.h"
#include "ppc-defs.h"

guest_t *guest = & (guest_t) { 0 };

err_t
guest_init(bool little, size_t ram_size)
{
  int i;
  err_t err;

  err = vmm_init(&(guest->vmm));
  if (err != ERR_NONE) {
    ERROR(err, "vmm_init");
    return err;
  }

  guest->vmm_index = guest->vmm->thread_index;
  guest->regs = &(guest->vmm->vmm_proc_state.ppcRegs.ppcRegs32);

  err = pmem_init(ram_size);
  if (err != ERR_NONE) {
    ERROR(err, "pmem_init");
    return err;
  }

  guest->pvr = 0x00010001; /* 601  - YES!   */
  // guest->pvr = 0x00030001; /* 603  - YES!   */
  // guest->pvr = 0x00040103; /* 604  - YES!   */
  // guest->pvr = 0x00090204; /* 604e - YES!   */
  // guest->pvr = 0x00080200; /* G3   - YES!!! */
  // guest->pvr = 0x000c0200; /* G4   - NOPE.  */

  for (i = 0; i < ARRAY_LEN(guest->sr); i++) {
    guest->sr[i] = (i << SR_VSID_SHIFT);
  }

  if (little) {
    guest->regs->ppcMSR |= MSR_LE;
  }

  return ERR_NONE;
}
