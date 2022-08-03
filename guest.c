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

  // guest->pvr = 0x00010001; /* 601  - YES!   */
  guest->pvr = 0x00030001; /* 603  - YES!   */
  // guest->pvr = 0x00040103; /* 604  - YES!   */
  // guest->pvr = 0x00090204; /* 604e - YES!   */
  // guest->pvr = 0x00080200; /* G3   - YES!!! */
  // guest->pvr = 0x000c0200; /* G4   - NOPE.  */

  for (i = 0; i < ARRAY_LEN(guest->sr); i++) {
    guest->sr[i] = (i << SR_VSID_SHIFT);
  }

  guest->regs->ppcMSR |= MSR_IR | MSR_DR;
  guest->mmu_state = MMU_PSEUDO_ON;

  if (little) {
    guest->regs->ppcMSR |= MSR_LE;
  }

  return ERR_NONE;
}

err_t
guest_map(ha_t host_address, gea_t ea)
{
  vmm_return_code_t vmm_ret;

  BUG_ON((host_address & PAGE_MASK) != 0, "bad alignment");
  BUG_ON((ea & PAGE_MASK) != 0, "bad alignment");

  vmm_ret = vmm_call(kVmmMapPage, guest->vmm_index,
                     host_address, ea, VM_PROT_ALL);
  ON_VMM_ERROR("kVmmMapPage", vmm_ret, out);
 out:
  if (vmm_ret != kVmmReturnNull) {
    return ERR_MACH;
  }

  return ERR_NONE;
}

err_t
guest_backmap(gea_t ea, gra_t *gra)
{
  ha_t ha_base;
  gea_t offset = ea & PAGE_MASK;

  ha_base = vmm_call(kVmmGetPageMapping, guest->vmm_index, ea);
  if (ha_base == (ha_t) -1) {
    return ERR_NOT_FOUND;
  }

  return pmem_gra(ha_base + offset, gra);
}

ha_t
guest_backmap_ha(gea_t ea)
{
  gra_t gra;
  if (guest_backmap(ea, &gra) != ERR_NONE) {
    return 0;
  }

  return pmem_ha(gra);
}

bool
guest_is_little(void)
{
  return (guest->regs->ppcMSR & MSR_LE) != 0;
}

bool
guest_mmu_allow_ra(void)
{
  return guest->mmu_state != MMU_ON;
}
