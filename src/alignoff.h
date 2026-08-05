#pragma once


#ifndef __cplusplus
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

#else

namespace align_utils {
    template<typename T, T align_size>
    inline T align_off(T base_off) {
        return (base_off + align_size - 1) &~ (align_size - 1);
    }
    template<typename T, T align_size>
    inline bool is_aligned_off(T base_off) {
        return align_off<T, align_size>(base_off) == base_off;
    }
    template<typename T>
    inline bool is_pow2(T num) {
        return (num > 0) && ((num & (num - 1)) == 0);
    }
    template<typename T>
    inline T num_next_pow2(T num) {
        if (!num) return 1u;
        return (sizeof(T) == sizeof(uint64_t)) ?
            1ULL << (64ULL - __builtin_clzll(static_cast<unsigned long long>(num - 1))) :
            1u << (32u - __builtin_clz(static_cast<unsigned int>(num - 1)));
    }
}


#endif // __cplusplus