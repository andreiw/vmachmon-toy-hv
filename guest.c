#define LOG_PFX GUEST
#include "guest.h"
#include "pmem.h"
#include "vmm.h"
#include "ppc-defs.h"
#include "rom.h"

guest_t *guest = & (guest_t) { 0 };

static void
guest_set_vmm(bool mmu_on)
{
  vmm_state_page_t *cur = guest->vmm;
  vmm_state_page_t *next = mmu_on ?
    guest->vmm_mmu_on :
    guest->vmm_mmu_off;
  vmm_regs32_t *next_regs =
    &(next->vmm_proc_state.ppcRegs.ppcRegs32);

  if (cur == next) {
    return;
  }

  if (cur != NULL) {
    next->vmm_proc_state = cur->vmm_proc_state;
  }

  guest->vmm = next;
  guest->regs = next_regs;

}

static void
guest_set_msr(uint32_t msr)
{
  bool ir = (msr & MSR_IR) != 0;
  bool dr = (msr & MSR_DR) != 0;

  BUG_ON((ir ^ dr) != 0, "inconsistent IR/DR");
  guest_set_vmm(ir | dr);

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
  guest->msr = msr;
}

void
guest_bye(void)
{
  kern_return_t kr;

  kr = vmm_call(kVmmTearDownContext,
                guest->vmm_mmu_on->thread_index);
  ON_MACH_ERROR("kVmmTearDownContext vmm_mmu_on", kr, done);

  kr = vmm_call(kVmmTearDownContext,
                guest->vmm_mmu_off->thread_index);
  ON_MACH_ERROR("kVmmTearDownContext vmm_mmu_on", kr, done);

 done:
  return;
}

err_t
guest_init(bool little, length_t ram_size)
{
  int i;
  err_t err;
  uint32_t guest_msr;

  err = vmm_init();
  ON_ERROR("vmm_init", err, done);

  err = vmm_init_vm(&(guest->vmm_mmu_off));
  ON_ERROR("vmm_init_vm mmu_off", err, done);

  err = vmm_init_vm(&(guest->vmm_mmu_on));
  ON_ERROR("vmm_init_vm mmu_on", err, done);

  err = pmem_init(ram_size);
  ON_ERROR("pmem_init", err, done);

  guest->pvr = PVR_601;
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
   * with HTAB. Instead, a magic value of SDR1 tells
   * guest_fault/guest_map to use rom_fault. When
   * SDR1 is SDR1_MAGIC_ROM_MODE, BAT/SR/HTAB-based
   * translation is not used (note: when adding BAT
   * support, honor BAT translations as well).
   */
  guest->sdr1 = SDR1_MAGIC_ROM_MODE;
  guest_msr |= MSR_MMU_ON;
  guest_set_msr(guest_msr);
 done:
  return err;
}

err_t
guest_map(ha_t host_address, gea_t ea)
{
  vmm_return_code_t vmm_ret;

  BUG_ON((host_address & PAGE_MASK) != 0, "bad alignment");
  BUG_ON((ea & PAGE_MASK) != 0, "bad alignment");

  vmm_ret = vmm_call(kVmmMapPage, guest->vmm->thread_index,
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

bool
guest_is_little(void)
{
  return (guest->mon_msr & MSR_LE) != 0;
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

static err_t
guest_backmap_ex(gea_t ea, gra_t *gra, bool try_fast)
{
  ha_t ha_base;
  gea_t offset = ea & PAGE_MASK;

  if ((guest->msr & MSR_MMU_ON) != MSR_MMU_ON) {
    if (pmem_gra_valid(ea)) {
      *gra = ea;
      return ERR_NONE;
    }

    return ERR_NOT_FOUND;
  }

  if (try_fast) {
    /*
     * This is a fast-path. The mapping may have not
     * been made yet with the VMM (via guest_fault) or
     * may have gotten evicted.
     */
    ha_base = vmm_call(kVmmGetPageMapping,
                       guest->vmm->thread_index, ea);
    if (ha_base != (ha_t) -1) {
      pmem_gra(ha_base + offset, gra);
      return ERR_NONE;
    }
  }

  if (guest->sdr1 == SDR1_MAGIC_ROM_MODE) {
    return rom_fault(ea, gra);
  }

  return ERR_NOT_FOUND;
}

err_t
guest_backmap(gea_t ea, gra_t *gra)
{
  return guest_backmap_ex(ea, gra, true);
}

err_t
guest_fault(void)
{
  gea_t gea;
  gra_t gra;
  err_t err;
  uint32_t dsisr;
  unsigned long *return_params32;

  return_params32 = guest->vmm->vmmRet.vmmrp32.return_params;
  gea = return_params32[0] & ~PAGE_MASK;
  dsisr = return_params32[1];

  if ((dsisr & DSISR_NOT_PRESENT) != 0) {
    err = guest_backmap_ex(gea, &gra, false);
    if (err != ERR_NONE) {
      ERROR(err, "guest_backmap_ex");
      return err;
    }

    err = guest_map(pmem_ha(gra), gea);
    if (err != ERR_NONE) {
      ERROR(err, "guest_map");
      return err;
    }

    return ERR_NONE;
  }

  return ERR_UNSUPPORTED;
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
