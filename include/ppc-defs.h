#pragma once

#define PPC_BITS(x) (1UL << (31 - x))

#define MSR_EE  PPC_BITS(16)  /* Extern Interrupts */
#define MSR_PR  PPC_BITS(17)  /* Privilege */
#define MSR_FP  PPC_BITS(18)  /* FP */
#define MSR_ME  PPC_BITS(19)  /* Machine Check */
#define MSR_FE0 PPC_BITS(20)  /* FP Exception Mode 0 */
#define MSR_SE  PPC_BITS(21)  /* Single Step */
#define MSR_FE1 PPC_BITS(23)  /* FP Exception Mode 1 */
#define MSR_EP  PPC_BITS(25)  /* Exception Prefix */
#define MSR_IR  PPC_BITS(26)  /* Instruction Relocate */
#define MSR_DR  PPC_BITS(27)  /* Data Relocate */
#define MSR_LE  PPC_BITS(31)  /* Little Endian (not 601) */

#define HID0_601_RESET_VALUE 0x80010080
#define HID0_601_LM PPC_BITS(28)

#define DSISR_NOT_PRESENT PPC_BITS(1)
#define DSISR_BAD_PERM    PPC_BITS(4)
#define DSISR_STORE       PPC_BITS(6)

/* 32-bit segment register definitions */
#define SR_INDEX(ea)            (ea >> 28)
#define SR_INDEX_TO_EA(index)   ((uint64_t) index << 28)
#define SR_COUNT                16
#define SR_T                    (1U << (31 - 0))
#define SR_KP                   (1U << (31 - 1))
#define SR_KS                   (1U << (31 - 2))
#define SR_VSID_MASK            0xFFFFFF
#define SR_VSID_SHIFT           0

#define INST_MTSPR_MASK         0xfc0007fe
#define INST_MFSPR_MASK         0xfc0007fe
#define INST_MTMSR_MASK         0xfc0007fe
#define INST_MFMSR_MASK         0xfc0007fe
#define INST_MFSR_MASK          0xfc0007fe
#define INST_RFI_MASK           0xfc0007fe

#define INST_MTSPR              0x7c0003a6
#define INST_MFSPR              0x7c0002a6
#define INST_MTMSR              0x7c000124
#define INST_MFMSR              0x7c0000a6
#define INST_MFSR               0x7c0004a6
#define INST_RFI                0x4c000064

#define SPRN_PVR        0x11f /* Processor Version Register */
