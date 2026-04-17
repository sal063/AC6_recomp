#pragma once
#include "ppc-inst.h"

/* A macro to extract the major opcode from an instruction.  */
#define PPC_OP(i) (((i) >> 26) & 0x3f)

/* A macro to extract the extended opcode from an instruction.  */
#define PPC_XOP(i) (((i) >> 1) & 0x3ff)

/* A macro to extract the branch destination from a conditional instruction. */
#define PPC_BD(i) ((signed int)((((i) & 0xFFFC) ^ 0x8000) - 0x8000))

/* A macro to extract the branch destination from an immediate instruction. */
#define PPC_BI(i) ((signed int)((((i) & 0x3FFFFFC) ^ 0x2000000) - 0x2000000))

/* A macro to extract whether the branch is absolute. */
#define PPC_BA(i) (!!((i) & 2))

/* A macro to extract whether the branch is a link. */
#define PPC_BL(i) (!!((i) & 1))

/* A macro to extract the branch operation of an instruction. */
#define PPC_BO(i) (((i) >> 21) & 0x1F)

#define PPC_OP_TDI 2
#define PPC_OP_TWI 3
#define PPC_OP_MULLI 7
#define PPC_OP_SUBFIC 8
#define PPC_OP_CMPLI 0xA
#define PPC_OP_CMPI 0xB
#define PPC_OP_ADDIC 0xC // addic
#define PPC_OP_ADDICR 0xD // addic.
#define PPC_OP_ADDI 0xE
#define PPC_OP_ADDIS 0xF
#define PPC_OP_BC 0x10
#define PPC_OP_SC 0x11
#define PPC_OP_B 0x12
#define PPC_OP_CTR 0x13
