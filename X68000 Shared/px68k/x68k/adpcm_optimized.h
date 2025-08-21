#ifndef ADPCM_OPTIMIZED_H
#define ADPCM_OPTIMIZED_H

#include "common.h"
#include "../fmgen/fmgen_simd.h"

// ADPCM optimization configuration
#define ADPCM_ENABLE_OPTIMIZATIONS 0  // Emergency disable - audio quality issues

// Optimization levels
// 0 = Original code
// 1 = Safe optimizations (cache, lookup tables)
// 2 = SIMD optimizations  
// 3 = Advanced optimizations (vectorization, unrolling)
#define ADPCM_OPTIMIZATION_LEVEL 1  // Start with safe optimizations

// Enable specific optimizations
#define ADPCM_OPTIMIZE_INTERPOLATION 1
#define ADPCM_OPTIMIZE_TABLES 1
#define ADPCM_OPTIMIZE_BUFFERS 1

// Performance analysis shows these bottlenecks:
// 1. INTERPOLATE macro - expensive 4-point cubic interpolation
// 2. dif_table lookups - frequent table access
// 3. Buffer management - circular buffer operations
// 4. Sample rate conversion - division/multiplication heavy

#if ADPCM_ENABLE_OPTIMIZATIONS

// Optimized interpolation with reduced precision for speed
#define INTERPOLATE_FAST(y, x) \
    (y[1] + (((y[2] - y[1]) * x) >> 8))

// Linear interpolation (faster but lower quality)
#define INTERPOLATE_LINEAR(y, x) \
    (y[1] + (((y[2] - y[1]) * (x)) / FM_IPSCALE))

// Branchless clamping
#define CLAMP_FAST(val, min, max) \
    ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

// Cache-optimized table structure
typedef struct {
    int dif_values[49*16] ALIGN_CACHE;
    int index_shifts[16] ALIGN_CACHE;
} ADPCM_Tables;

// Optimized buffer management
typedef struct {
    signed short* bufferR ALIGN_CACHE;
    signed short* bufferL ALIGN_CACHE;
    long writePtr;
    long readPtr;
    long size;
    long mask; // Power of 2 size for fast modulo
} ADPCM_Buffer;

// Function prototypes for optimized versions
// Note: Functions are static and included directly in adpcm.c

#ifdef FMGEN_USE_NEON
// NEON-optimized interpolation for ARM
void ADPCM_InterpolateNEON(int* input, int* output, int count, int ratio);
#endif

#ifdef FMGEN_USE_SSE  
// SSE-optimized interpolation for x86
void ADPCM_InterpolateSSE(int* input, int* output, int count, int ratio);
#endif

#endif // ADPCM_ENABLE_OPTIMIZATIONS

#endif // ADPCM_OPTIMIZED_H