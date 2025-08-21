#ifndef FMGEN_CONFIG_H
#define FMGEN_CONFIG_H

// Enable optimizations
#define FMGEN_ENABLE_OPTIMIZATIONS 1

// Choose optimization level
// 0 = Original code
// 1 = Safe optimizations (current)
// 2 = Advanced optimizations (experimental) 
// 3 = SIMD optimizations (future)
#define FMGEN_OPTIMIZATION_LEVEL 1  // Confirmed working

// Cache optimization settings
#define FMGEN_CACHE_ALIGN 1

// Prefetch distance for table lookups
#define FMGEN_PREFETCH_DISTANCE 4

#endif // FMGEN_CONFIG_H