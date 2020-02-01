//
// Created by Dustin Dettmer on 2020-01-13.
//

#if __BIG_ENDIAN__ || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || __ARMEB__ || __THUMBEB__ || __AARCH64EB__ || __MIPSEB__
#define WORDS_BIGENDIAN        1
#endif

#define DETERMINISTIC 1
#define USE_BASIC_CONFIG 1
#define ENABLE_MODULE_RECOVERY 1

#undef USE_ASM_X86_64
#undef USE_ENDOMORPHISM
#undef USE_FIELD_10X26
#undef USE_FIELD_5X52
#undef USE_FIELD_INV_BUILTIN
#undef USE_FIELD_INV_NUM
#undef USE_NUM_GMP
#undef USE_NUM_NONE
#undef USE_SCALAR_4X64
#undef USE_SCALAR_8X32
#undef USE_SCALAR_INV_BUILTIN
#undef USE_SCALAR_INV_NUM

#define USE_NUM_NONE 1
#define USE_FIELD_INV_BUILTIN 1
#define USE_SCALAR_INV_BUILTIN 1
#define USE_FIELD_10X26 1
#define USE_SCALAR_8X32 1
#define ECMULT_WINDOW_SIZE 15
#define ECMULT_GEN_PREC_BITS 8

#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wconditional-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-function"

#include "secp256k1/src/secp256k1.c"
#include "secp256k1/src/modules/ecdh/main_impl.h"
