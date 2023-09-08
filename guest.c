#define LOG_PFX GUEST
#include "guest.h"
#include "pmem.h"
#include "vmm.h"
#include "ppc-defs.h"
#include "rom.h"

guest_t *guest = & (guest_t) { 0 };

void
guest_mon_dump(void)
{
  int i;
  unsigned long *return_params32;
  uint32_t insn = -1;

  guest_from_x(&insn, guest->regs->ppcPC);

  mon_printf("VMM state:\n");
  mon_printf("  MSR                   = 0x%08x\n", guest->regs->ppcMSR);
  mon_printf("  vmm_state_page_t *vmm = mmu_o%s\n",
             guest->vmm == guest->vmm_mmu_on ? "n" : "ff");
  mon_printf("  return_code           = 0x%08x (%s)\n",
      guest->vmm->return_code, vmm_return_code_to_string(guest->vmm->return_code));

  return_params32 = guest->vmm->vmmRet.vmmrp32.return_params;

  for (i = 0; i < 4; i++) {
    mon_printf("  return_params32[%d]    = 0x%08lx (%lu)\n", i,
        return_params32[i], return_params32[i]);
  }

  mon_printf("OEA:\n");
  mon_printf("  PVR  = 0x%08x\n", guest->pvr);
  mon_printf("  MSR  = 0x%08x\n", guest->msr);
  mon_printf("  SDR1 = 0x%08x\n", guest->sdr1);
  mon_printf("  HID0 = 0x%08x\n", guest->hid0);
  mon_printf("  SRR0 = 0x%08x SRR1 = 0x%08x\n",
             guest->srr0, guest->srr1);
  for (i = 0; i < ARRAY_LEN(guest->sr); i += 2) {
    mon_printf("  SR%-2d = 0x%08x SR%-2d = 0x%08x\n",
               i, guest->sr[i], i + 1, guest->sr[i + 1]);
  }

  mon_printf("UISA:\n");
  mon_printf("  PC                   = %p (%lu)\n",
      (void *)guest->regs->ppcPC, guest->regs->ppcPC);
  mon_printf("  Instruction at PC    = 0x%08x\n", insn);
  mon_printf("  CR                   = 0x%08x\n", guest->regs->ppcCR);
  mon_printf("  LR                   = 0x%08x (%lu)\n",
             guest->regs->ppcLR, guest->regs->ppcLR);
  mon_printf("  CTR                  = 0x%08x (%lu)\n",
             guest->regs->ppcCTR, guest->regs->ppcCTR);
  mon_printf("  XER                  = 0x%08x\n",
             guest->regs->ppcXER);
  mon_printf("  FPSCR                = 0x%08x\n",
             guest->vmm->vmm_proc_state.ppcFPSCR);

  for (i = 0; i < 16; i++) {
    mon_printf("  r%-2d = 0x%08x r%-2d = 0x%08x\n",
        i * 2, guest->regs->ppcGPRs[i * 2],
        i * 2 + 1, guest->regs->ppcGPRs[i * 2 + 1]);
  }
}

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
   * Set up the 601 TLBs.
   * UTLB: 256-entry, two-way set-associative.
   * ITLB: four entry.
   */

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

void
guest_unmap(gea_t ea)
{
  kern_return_t ret;

  ea &= ~PAGE_MASK;

  ret = vmm_call(kVmmUnmapPage, guest->vmm_mmu_on->thread_index, ea);
  if (ret != KERN_SUCCESS) {
    WARN("failed to unmap 0x%x", ea);
  }
}

void
guest_unmap_all(void)
{
  /*
   * According to vmachmon.c, returns nothing.
   */
  vmm_call(kVmmUnmapAllPages, guest->vmm_mmu_on->thread_index);
}

err_t
guest_map(ha_t host_address, gea_t ea)
{
  vmm_return_code_t vmm_ret;

  BUG_ON((host_address & PAGE_MASK) != 0, "bad alignment");
  BUG_ON((ea & PAGE_MASK) != 0, "bad alignment");

  vmm_ret = vmm_call(kVmmMapPage, guest->vmm->thread_index,
                     host_address, ea, VM_PROT_ALL);

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

    xferred = pmem_from_ex(d, gra, xfer_size, access_size, nul_term ?
                           PMEM_FROM_NUL_TERM : 0);
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
guest_bat_fault(gea_t ea, gra_t *gra)
{
  int i;

  BUG_ON(guest->pvr != PVR_601, "unsupported BAT handling");

  /*
   * BAT on 601 is enabled only when the matching
   * SR T = 0 (i.e. not I/O controller interface
   * segs). This is diferent from other PowerPC
   * processors, as the OEA defines BAT always
   * taking precedence over any segment translation,
   * independent of the T bit.
   */
  if ((guest->sr[SR_INDEX(ea)] & SR_T) == 1) {
    return ERR_UNSUPPORTED;
  }

  for (i = 0; i < ARRAY_LEN(guest->ubat); i += 2) {
    uint32_t batu = guest->ubat[i];
    uint32_t blpi = PPC_MASK_OFF (batu, 0, 14);

    BUG_ON (blpi == PPC_MASK_OFF (ea, 0, 14), "BAT hit, implement BAT support");
  }

  return ERR_UNSUPPORTED;
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

  if (guest_bat_fault(ea, gra) == ERR_NONE) {
    return ERR_NONE;
  }

  ERROR(ERR_UNSUPPORTED, "time to wire up SR/SDR1 decoding for 0x%lx (SDR1 0x%lx)", ea, guest->sdr1);
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
  bool update_insn = true;

  err = guest_from_x(&insn, guest->regs->ppcPC);
  ON_ERROR("read insn", err, done);

#define R(x) guest->regs->ppcGPRs[x]

  err = ERR_UNSUPPORTED;

  /*
   * Unfortunately NT4.0 kernel _is_ MP, and it does an mfsprg between
   * an lwarx and stwcx.
   *
   * grep -C4 lwarx nt.txt | grep mfsp
   */

  if (insn == 0x7d7042a6) {
    /*
     * Read from sprg[0] into r5, write r5 to [r10].
     */
    uint32_t cmp[4];
    uint32_t insn[4] = { 0x40820014, 0x7d60512d, 0x4082000c, 0x4c00012c };

    guest_from_ex(cmp, guest->regs->ppcPC + 4, sizeof(cmp), sizeof(uint32_t), false);
    if (!memcmp(cmp, insn, sizeof(cmp))) {
      guest_to_x(R(10), &guest->sprg[0]);
      guest->regs->ppcPC += 10 * sizeof(uint32_t);
      VERBOSE("skipped lock seq 1");
      return ERR_NONE;
    }
  } else if (insn == 0x7cb042a6) {
    /*
     * Read from sprg[0] into r5, write r5 to [r3].
     */
    uint32_t cmp[4];
    uint32_t insn[4] = { 0x40820024, 0x7ca0192d, 0x4082001c, 0x4c00012c };

    guest_from_ex(cmp, guest->regs->ppcPC + 4, sizeof(cmp), sizeof(uint32_t), false);
    if (!memcmp(cmp, insn, sizeof(cmp))) {
      guest_to_x(R(3), &guest->sprg[0]);
      guest->regs->ppcPC += 5 * sizeof(uint32_t);
      VERBOSE("skipped lock seq 2");
      return ERR_NONE;
    }
  } else if (insn == 0x7cf042a6) {
    /*
     * Read from sprg[0] into r7, write r7 to [r9].
     */
    uint32_t cmp[4];
    uint32_t insn[4] = { 0x40820014, 0x7ce0492d, 0x4082000c, 0x4c00012c };

    guest_from_ex(cmp, guest->regs->ppcPC + 4, sizeof(cmp), sizeof(uint32_t), false);
    if (!memcmp(cmp, insn, sizeof(cmp))) {
      guest_to_x(R(9), &guest->sprg[0]);
      guest->regs->ppcPC += 5 * sizeof(uint32_t);
      VERBOSE("skipped lock seq 3/4");
      return ERR_NONE;
    }
  }

  if ((insn & INST_TLBIE_MASK) == INST_TLBIE) {
    uint32_t nexti = 0;
    int reg = PPC_MASK_OFF(insn, 16, 20);
    gea_t ea = R(reg);
    /*
     * Valid for NT, heh.
     *
     * Need to check Linux.
     */
    guest_from_x(&nexti, guest->regs->ppcPC + 4);
    if (nexti == INST_SYNC) {
      guest_unmap_all();
    } else {
      guest_unmap(ea);
    }
    err = ERR_NONE;
  } if ((insn & INST_MFSR_MASK) == INST_MFSR) {
    int reg = PPC_MASK_OFF(insn, 6, 10);
    int sr = PPC_MASK_OFF(insn, 12, 15);
    R(reg) = guest->sr[sr];
    err = ERR_NONE;
  } else if ((insn & INST_MTSR_MASK) == INST_MTSR) {
    int reg = PPC_MASK_OFF(insn, 6, 10);
    int sr = PPC_MASK_OFF(insn, 12, 15);
    guest->sr[sr] = R(reg);
    err = ERR_NONE;
  } else if ((insn & INST_RFI_MASK) == INST_RFI) {
    update_insn = false;
    guest_set_msr(guest->srr1);
    guest->regs->ppcPC = guest->srr0;
    err = ERR_NONE;
  } else if ((insn & INST_MFSPR_MASK) == INST_MFSPR) {
    int reg = PPC_MASK_OFF(insn, 6, 10);
    int spr = PPC_MASK_OFF(insn, 11, 20);
    spr = ((spr & 0x1f) << 5) | ((spr & 0x3e0) >> 5);
    switch (spr) {
    case SPRN_PVR:
      R(reg) = guest->pvr;
      err = ERR_NONE;
      break;
    case SPRN_SRR0:
      R(reg) = guest->srr0;
      err = ERR_NONE;
      break;
    case SPRN_SRR1:
      R(reg) = guest->srr1;
      err = ERR_NONE;
      break;
    case SPRN_IBAT0U:
    case SPRN_IBAT0L:
    case SPRN_IBAT1U:
    case SPRN_IBAT1L:
    case SPRN_IBAT2U:
    case SPRN_IBAT2L:
    case SPRN_IBAT3U:
    case SPRN_IBAT3L: {
      uint32_t *batp = guest->ubat;
      R(reg) = batp[spr - SPRN_IBAT0U];
      err = ERR_NONE;
      break;
    }
    case SPRN_DBAT0U:
    case SPRN_DBAT0L:
    case SPRN_DBAT1U:
    case SPRN_DBAT1L:
    case SPRN_DBAT2U:
    case SPRN_DBAT2L:
    case SPRN_DBAT3U:
    case SPRN_DBAT3L: {
      WARN("access to non-existing DBAT SPR %u", spr);
      R(reg) = 0;
      err = ERR_NONE;
      break;
    }
    case SPRN_SPRG0:
    case SPRN_SPRG1:
    case SPRN_SPRG2:
    case SPRN_SPRG3: {
      uint32_t *sprg = guest->sprg;
      R(reg) = sprg[spr - SPRN_SPRG0];
      err = ERR_NONE;
      break;
    }
    case SPRN_SDR1: {
      R(reg) = guest->sdr1;
      err = ERR_NONE;
      break;
    }
    default:
      WARN("0x%x: unhandled MFSPR r%u, %u",
           guest->regs->ppcPC, reg, spr);
      return ERR_UNSUPPORTED;
    }
  } else if ((insn & INST_MTSPR_MASK) == INST_MTSPR) {
    int reg = PPC_MASK_OFF(insn, 6, 10);
    int spr = PPC_MASK_OFF(insn, 11, 20);
    spr = ((spr & 0x1f) << 5) | ((spr & 0x3e0) >> 5);
    switch (spr) {
    case SPRN_SRR0:
      guest->srr0 = R(reg);
      err = ERR_NONE;
      break;
    case SPRN_SRR1:
      guest->srr1 = R(reg);
      err = ERR_NONE;
      break;
    case SPRN_IBAT0U:
    case SPRN_IBAT0L:
    case SPRN_IBAT1U:
    case SPRN_IBAT1L:
    case SPRN_IBAT2U:
    case SPRN_IBAT2L:
    case SPRN_IBAT3U:
    case SPRN_IBAT3L: {
      uint32_t *batp = guest->ubat;
      batp[spr - SPRN_IBAT0U] = R(reg);
      err = ERR_NONE;
      break;
    }
    case SPRN_SPRG0:
    case SPRN_SPRG1:
    case SPRN_SPRG2:
    case SPRN_SPRG3: {
      uint32_t *sprg = guest->sprg;
      sprg[spr - SPRN_SPRG0] = R(reg);
      err = ERR_NONE;
      break;
    }
    case SPRN_SDR1: {
      if (R(reg) != SDR1_MAGIC_ROM_MODE &&
          guest->sdr1 == SDR1_MAGIC_ROM_MODE) {
        /*
         * Since this doesn't use the regular SR/HTAB/BAT path.
         */
        guest_unmap_all();
      }
      guest->sdr1 = R(reg);
      err = ERR_NONE;
      break;
    }
    default:
      WARN("0x%x: unhandled MTSPR %u, r%u",
           guest->regs->ppcPC, spr ,reg);
      return ERR_UNSUPPORTED;
    }
  } else if ((insn & INST_MFMSR_MASK) == INST_MFMSR) {
    int reg = PPC_MASK_OFF(insn, 6, 10);
    R(reg) = guest->msr;
    err = ERR_NONE;
  } else if ((insn & INST_MTMSR_MASK) == INST_MTMSR) {
    int reg = PPC_MASK_OFF(insn, 6, 10);
    guest_set_msr(R(reg));
    err = ERR_NONE;
  }

 done:
  if (err == ERR_NONE) {
    if (update_insn) {
      guest->regs->ppcPC += 4;
    }
  } else {
    WARN("0x%x: unhandled instruction 0x%x",
         guest->regs->ppcPC, insn);
  }
  return err;
}
