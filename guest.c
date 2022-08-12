#define LOG_PFX GUEST
#include "guest.h"
#include "pmem.h"
#include "vmm.h"
#include "ppc-defs.h"

guest_t *guest = & (guest_t) { 0 };

static void
guest_set_msr(uint32_t msr)
{
  guest->msr = msr;
  /*
   * The VMM will set/clear bits as necessary,
   * as we only get to control VEC, FP, FE0, FE1, SE, BE, PM and LE.
   *
   * So we can be lazy.
   *
   * The VMM will leak the actual MSR when returning. For
   * example, it always sets IR, DR, EE, ME and PR.
   *
   * This is useful for debugging.
   */
  guest->regs->ppcMSR = msr | guest->mon_msr;
}

err_t
guest_init(bool little, length_t ram_size)
{
  int i;
  err_t err;
  uint32_t guest_msr;

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

  /*
   * The only 601 register with a non-trivial
   * reset value.
   */
  guest->hid0 = HID0_601_RESET_VALUE;

  /*
   * 601 doc says EP and ME are set on hard reset.
   */
  guest_msr = MSR_ME | MSR_EP;
  guest->mon_msr = 0;
  if (little) {
    /*
     * Not into guest_msr, as LE exposed differently
     * on a 601.
     */
    guest->mon_msr |= MSR_LE;
    guest->hid0 |= HID0_601_LM;
  }
  /*
   * rom.c emulates OF with MMU, without bothering
   * with HTAB. Maybe this will go away when the
   * HTAB emulation is done.
   */
  guest_msr |= MSR_IR | MSR_DR;
  guest->mmu_state = MMU_PSEUDO_ON;
  guest_set_msr(guest_msr);

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

  /*
   * Apparently (looking at the sources) this is a
   * kern_err_t...
   */
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

  if (guest_mmu_allow_ra()) {
    if (pmem_gra_valid(ea)) {
      *gra = ea;
      return ERR_NONE;
    }
  }

  ha_base = vmm_call(kVmmGetPageMapping, guest->vmm_index, ea);
  if (ha_base == (ha_t) -1) {
    return ERR_NOT_FOUND;
  }

  return pmem_gra(ha_base + offset, gra);
}

bool
guest_is_little(void)
{
  return (guest->mon_msr & MSR_LE) != 0;
}

bool
guest_mmu_allow_ra(void)
{
  return guest->mmu_state != MMU_ON;
}

bool
guest_toggle_ss(void)
{
  guest->mon_msr ^= MSR_SE;
  guest_set_msr(guest->msr);
  return (guest->mon_msr & MSR_SE) != 0;
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

    xferred = pmem_to(gra, s, xfer_size, access_size);
    left -= xferred;

    if (xferred != xfer_size) {
      WARN("unexpected truncated transfer to EA 0x%x GRA 0x%x, %u left",
           dest, gra, left);
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

err_t
guest_emulate(void)
{
  err_t err;
  uint32_t insn;

  err = guest_from_x(&insn, guest->regs->ppcPC);
  ON_ERROR("read insn", err, done);

#define R(x) guest->regs->ppcGPRs[x]

  err = ERR_UNSUPPORTED;
  if ((insn & INST_MFSPR_MASK) == INST_MFSPR) {
    int reg = MASK_OFF(insn, 25, 21);
    int spr = MASK_OFF(insn, 20, 11);
    spr = ((spr & 0x1f) << 5) | ((spr & 0x3e0) >> 5);
    switch (spr) {
    case SPRN_PVR:
      R(reg) = guest->pvr;
      err = ERR_NONE;
      break;
    default:
      WARN("0x%x: unhandled MFSPR r%u, %u",
           guest->regs->ppcPC, reg, spr);
      return ERR_UNSUPPORTED;
    }
  } else if ((insn & INST_MFMSR_MASK) == INST_MFMSR) {
    int reg = MASK_OFF(insn, 25, 21);
    R(reg) = guest->msr;
    err = ERR_NONE;
  }

 done:
  if (err == ERR_NONE) {
    guest->regs->ppcPC += 4;
  } else {
    WARN("0x%x: unhandled instruction 0x%x",
         guest->regs->ppcPC, insn);
  }
  return err;
}
