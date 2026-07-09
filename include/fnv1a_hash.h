#pragma once
#include <stddef.h>
#include <stdint.h>

#define FNV1A_OFFSET_BASIS_U64  0xcbf29ce484222325ULL
#define FNV1A_PRIME_U64         0x00000100000001b3ULL
#define FNV1A_OFFSET_BASIS_U32  0x811C9DC5u
#define FNV1A_PRIME_U32         0x01000193u

static inline uint64_t fnv_1a_hash64(const void* key, size_t len) {
    uint64_t h = FNV1A_OFFSET_BASIS_U64;
    size_t i = 0;
    while (i < len) {
        h ^= ((uint8_t*)key)[i++];
        h *= FNV1A_PRIME_U64;
    }
    return h;
}

static inline uint32_t fnv_1a_hash32(const void* key, size_t len) {
    uint32_t h = FNV1A_OFFSET_BASIS_U32;
    size_t i = 0;
    while (i < len) {
        h ^= ((uint8_t*)key)[i++];
        h *= FNV1A_PRIME_U32;
    }
    return h;
}

