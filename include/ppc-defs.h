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

#define PVR_601  0x00010001
#define PVR_603  0x00030001
#define PVR_604  0x00040103
#define PVR_604e 0x00090204
#define PVR_G3   0x00080200
#define PVR_G4   0x000c0200 /* Not supported by NT */

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
#define INST_MTSR_MASK          0xfc0007fe
#define INST_RFI_MASK           0xfc0007fe
#define INST_TLBIE_MASK         0xfc0007fe

#define INST_MTSPR              0x7c0003a6
#define INST_MFSPR              0x7c0002a6
#define INST_MTMSR              0x7c000124
#define INST_MFMSR              0x7c0000a6
#define INST_MTSR               0x7c0001a4
#define INST_MFSR               0x7c0004a6
#define INST_RFI                0x4c000064
#define INST_TLBIE              0x7c000264
#define INST_SYNC               0x7c0004ac

/* User-level SPRs */
#define SPRN_601_MQ     0
#define SPRN_RTCU       4
#define SPRN_RTCL       5

/* Supervisor-level SPRs */
#define SPRN_DSISR      18
#define SPRN_DAR        19
#define SPRN_RTCU_WRITE 20
#define SPRN_RTCU_READ  21
#define SPRN_DEC        22
#define SPRN_SDR1       25
#define SPRN_SRR0       26  /* Save/Restore Register 0 */
#define SPRN_SRR1       27  /* Save/Restore Register 1 */
#define SPRN_SPRG0      272
#define SPRN_SPRG1      273
#define SPRN_SPRG2      274
#define SPRN_SPRG3      275
#define SPRN_EAR        282
#define SPRN_PVR        287 /* Processor Version Register */
#define SPRN_IBAT0U     528
#define SPRN_IBAT0L     529
#define SPRN_IBAT1U     530
#define SPRN_IBAT1L     531
#define SPRN_IBAT2U     532
#define SPRN_IBAT2L     533
#define SPRN_IBAT3U     534
#define SPRN_IBAT3L     535
#define SPRN_DBAT0U     536
#define SPRN_DBAT0L     537
#define SPRN_DBAT1U     538
#define SPRN_DBAT1L     539
#define SPRN_DBAT2U     540
#define SPRN_DBAT2L     541
#define SPRN_DBAT3U     542
#define SPRN_DBAT3L     543

#define SPRN_601_HID0   1008
#define SPRN_601_HID1   1009
#define SPRN_601_IABR   1010
#define SPRN_601_DABR   1013
