#pragma once

#define ALIGN_SIZE_DEFAULT 16

#define align_off(off, align_size) (\
    ((off) + (align_size) - 1) &~ ((align_size) - 1)\
)

#define is_aligned_off(off, align_size) (\
    (align_off(off, align_size) == off)\
)

#define is_pow2(num) (\
    ((num) > 0) && \
    (((num) & ((num) - 1)) == 0)\
)

#include <stdint.h>

static inline uint32_t u32_next_pow2(uint32_t x) {
    return (!x) ? 1u : 1u << (32 - __builtin_clz(x - 1));
}

static inline uint64_t u64_next_pow2(uint64_t x) {
    return (!x) ? 1ULL : 1ULL << (64ULL - __builtin_clz(x - 1));
}