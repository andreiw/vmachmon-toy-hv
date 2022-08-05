#pragma once

#define PPC_BITS(x) (1UL << (31 - x))

#define MSR_SE PPC_BITS(21) /* Single Step */
#define MSR_IR PPC_BITS(26) /* Instruction Relocate */
#define MSR_DR PPC_BITS(27) /* Data Relocate */
#define MSR_LE PPC_BITS(31) /* Little Endian */

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

// PowerPC instruction template: add immediate, D-form
typedef struct I_addi_d_form {
    u_int32_t OP: 6;  // major opcode
    u_int32_t RT: 5;  // target register
    u_int32_t RA: 5;  // register operand
    u_int32_t SI: 16; // immediate operand
} I_addi_d_form;

// PowerPC instruction template: unconditional branch, I-form
typedef struct branch_i_form {
    u_int32_t OP: 6;  // major opcode
    u_int32_t LI: 24; // branch target (immediate)
    u_int32_t AA: 1;  // absolute or relative
    u_int32_t LK: 1;  // link or not
} I_branch_i_form;

// PowerPC instruction template: add, XO-form
typedef struct I_add_xo_form {
    u_int32_t OP: 6;  // major opcode
    u_int32_t RT: 5;  // target register
    u_int32_t RA: 5;  // register operand A
    u_int32_t RB: 5;  // register operand B
    u_int32_t OE: 1;  // alter SO, OV?
    u_int32_t XO: 9;  // extended opcode
    u_int32_t Rc: 1;  // alter CR0?
} I_add_xo_form;
