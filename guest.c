#include "guest.h"
#include "pmem.h"
#include "vmm.h"
#include "ppc-defs.h"

guest_t *guest = & (guest_t) { 0 };

err_t
guest_init(bool little, length_t ram_size)
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

length_t
guest_from_ex(void *dest,
              gea_t src,
              length_t bytes,
              length_t access_size,
              bool nul_term)
{
  length_t left = bytes;
  uint8_t *d = dest;

  BUG_ON(nul_term && access_size != 1, "invalid access_size and nul_term");

  do {
    gra_t gra;
    length_t xferred;
    length_t xfer_size = min(PAGE_SIZE - (src & PAGE_MASK), left);

    if (guest_backmap(src, &gra) != ERR_NONE) {
      WARN("backmap failure for 0x%x", src);
      break;
    }

    xferred = pmem_from_ex(d, gra, xfer_size, access_size, nul_term);
    left -= xferred;

    if (xferred != xfer_size) {
      if (!nul_term) {
        WARN("unexpected truncated transfer, %u left", left);
      }
      break;
    }

    d += xfer_size;
    src += xfer_size;
  } while (left != 0);

  return bytes - left;
}

err_t
guest_from(void *dest,
           gea_t src,
           length_t bytes,
           length_t access_size)
{
  length_t xferred = guest_from_ex(dest, src, bytes, access_size, false);

  if (xferred != bytes) {
    return ERR_BAD_ACCESS;
  }

  return ERR_NONE;
}

err_t
guest_to(gea_t dest,
         const void *src,
         length_t bytes,
         length_t access_size)
{
  length_t left = bytes;
  const uint8_t *s = src;

  do {
    gra_t gra;
    length_t xferred;
    length_t xfer_size = min(PAGE_SIZE - (dest & PAGE_MASK), left);

    if (guest_backmap(dest, &gra) != ERR_NONE) {
      WARN("backmap failure for 0x%x", dest);
      break;
    }

    xferred = pmem_to(dest, s, xfer_size, access_size);
    left -= xferred;

    if (xferred != xfer_size) {
      WARN("unexpected truncated transfer, %u left", left);
      break;
    }

    s += xfer_size;
    dest += xfer_size;
  } while (left != 0);

  if (left != 0) {
    return ERR_BAD_ACCESS;
  }

  return ERR_NONE;
}

void
guest_dump(void)
{
  int i, j;
  uint32_t insn;
  unsigned long *return_params32;

  LOG("Processor state:");
  guest_from_x(&insn, guest->regs->ppcPC);

  LOG("  PC                   = %p (%lu)",
      (void *)guest->regs->ppcPC, guest->regs->ppcPC);
  LOG("  Instruction at PC    = 0x%08x", insn);
  LOG("  CR                   = 0x%08x"
      "                         ", guest->regs->ppcCR);
  LOG("  LR                   = 0x%08x (%lu)",
      guest->regs->ppcLR, guest->regs->ppcLR);
  LOG("  MSR                  = 0x%08x"
      "                         ", guest->regs->ppcMSR);
  LOG("  return_code          = 0x%08x (%s)",
      guest->vmm->return_code, vmm_return_code_to_string(guest->vmm->return_code));

  return_params32 = guest->vmm->vmmRet.vmmrp32.return_params;

  for (i = 0; i < 4; i++)
    LOG("  return_params32[%d]   = 0x%08lx (%lu)", i,
        return_params32[i], return_params32[i]);

  LOG("  GPRs:");
  for (j = 0; j < 16; j++) {
    LOG("r%-2d = 0x%08x r%-2d = 0x%08x",
        j * 2, guest->regs->ppcGPRs[j * 2],
        j * 2 + 1, guest->regs->ppcGPRs[j * 2 + 1]);
  }
}
