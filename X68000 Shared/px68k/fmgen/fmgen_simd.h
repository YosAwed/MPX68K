#ifndef FMGEN_SIMD_H
#define FMGEN_SIMD_H

#include "fmgen_types.h"

// Platform detection and SIMD support
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(__arm64__)
    #define FMGEN_USE_NEON 1
    #include <arm_neon.h>
#elif defined(__SSE2__)
    #define FMGEN_USE_SSE 1
    #include <immintrin.h>
#else
    #define FMGEN_USE_SCALAR 1
#endif

// Branchless helpers
inline int32 branchless_select(int32 condition, int32 if_true, int32 if_false) {
    // Create mask: -1 if condition != 0, 0 otherwise
    int32 mask = -(condition != 0);
    return (if_true & mask) | (if_false & ~mask);
}

inline int32 branchless_add_if(int32 base, int32 addend, int32 condition) {
    // Add 'addend' to 'base' only if condition is true
    int32 mask = -(condition != 0);
    return base + (addend & mask);
}

#ifdef FMGEN_USE_NEON
// ARM NEON optimizations for Apple Silicon

// Process 4 channels at once
inline void calc_4_channels_neon(int32* results, void** channels, int32 activech_mask) {
    // We'll implement the actual channel calculation here
    int32x4_t mask = vdupq_n_s32(0);
    int32x4_t result = vdupq_n_s32(0);
    
    // Store results
    vst1q_s32(results, result);
}

// Prefetch next cache line
inline void prefetch_next(const void* addr) {
    __builtin_prefetch(addr, 0, 3);
}

#elif defined(FMGEN_USE_SSE)
// x86 SSE optimizations for Intel Macs

inline void calc_4_channels_sse(__m128i* results, void** channels, int32 activech_mask) {
    __m128i mask = _mm_setzero_si128();
    __m128i result = _mm_setzero_si128();
    
    _mm_store_si128(results, result);
}

inline void prefetch_next(const void* addr) {
    _mm_prefetch((const char*)addr, _MM_HINT_T0);
}

#else
// Scalar fallback

inline void prefetch_next(const void* addr) {
    // No-op for scalar
    (void)addr;
}

#endif

// Cache-optimized data alignment
#define CACHE_LINE_SIZE 64
#define ALIGN_CACHE __attribute__((aligned(CACHE_LINE_SIZE)))

#endif // FMGEN_SIMD_H