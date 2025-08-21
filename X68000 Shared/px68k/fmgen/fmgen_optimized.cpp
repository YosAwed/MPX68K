// Optimized implementations for FM synthesis
#include "fmgen.h"
#include "fmgen_simd.h"
#include "fmgen_config.h"

namespace FM {

// Cache-aligned table access with prefetching
#if FMGEN_CACHE_ALIGN
static int32 sinetable_aligned[FM_OPSINENTS] ALIGN_CACHE;
static int32 cltable_aligned[FM_CLENTS] ALIGN_CACHE;

void InitAlignedTables() {
    // Copy tables to cache-aligned memory
    memcpy(sinetable_aligned, Operator::sinetable, sizeof(Operator::sinetable));
    memcpy(cltable_aligned, Operator::cltable, sizeof(Operator::cltable));
}
#endif

// Optimized LogToLin with prefetching
inline FM::ISample Operator::LogToLinOptimized(uint a)
{
#if FMGEN_CACHE_ALIGN
    // Prefetch next likely entries
    prefetch_next(&cltable_aligned[a + FMGEN_PREFETCH_DISTANCE]);
    return (a < FM_CLENTS) ? cltable_aligned[a] : 0;
#else
    prefetch_next(&cltable[a + FMGEN_PREFETCH_DISTANCE]);
    return (a < FM_CLENTS) ? cltable[a] : 0;
#endif
}

// Optimized sine table lookup
#define SINE_OPTIMIZED(s) \
    (prefetch_next(&sinetable_aligned[(s + FMGEN_PREFETCH_DISTANCE) & (FM_OPSINENTS-1)]), \
     sinetable_aligned[(s) & (FM_OPSINENTS-1)])

// Branchless Calc implementation
inline FM::ISample FM::Operator::CalcOptimized(ISample in)
{
    EGStep();
    out2_ = out_;
    
    int pgin = PGCalc() >> (20+FM_PGBITS-FM_OPSINBITS);
    pgin += in >> (20+FM_PGBITS-FM_OPSINBITS-(2+IS2EC_SHIFT));
    
#if FMGEN_CACHE_ALIGN
    out_ = LogToLinOptimized(eg_out_ + SINE_OPTIMIZED(pgin));
#else
    out_ = LogToLin(eg_out_ + SINE(pgin));
#endif
    
    dbgopout_ = out_;
    return out_;
}

#ifdef FMGEN_USE_NEON
// NEON SIMD version for processing multiple operators
void ProcessOperatorsSIMD(Operator* ops, int count, ISample* inputs, ISample* outputs)
{
    // Process 4 operators at once
    for (int i = 0; i < count; i += 4) {
        // Load EG states
        int32x4_t eg_out = vld1q_s32((int32_t*)&ops[i].eg_out_);
        
        // Calculate phase
        int32x4_t phases = vdupq_n_s32(0);
        for (int j = 0; j < 4 && (i+j) < count; j++) {
            ops[i+j].EGStep();
            int pgin = ops[i+j].PGCalc() >> (20+FM_PGBITS-FM_OPSINBITS);
            pgin += inputs[i+j] >> (20+FM_PGBITS-FM_OPSINBITS-(2+IS2EC_SHIFT));
            phases = vsetq_lane_s32(pgin, phases, j);
        }
        
        // Lookup sine values (this part still needs individual lookups)
        int32x4_t sine_vals;
        sine_vals = vsetq_lane_s32(SINE(vgetq_lane_s32(phases, 0)), sine_vals, 0);
        sine_vals = vsetq_lane_s32(SINE(vgetq_lane_s32(phases, 1)), sine_vals, 1);
        sine_vals = vsetq_lane_s32(SINE(vgetq_lane_s32(phases, 2)), sine_vals, 2);
        sine_vals = vsetq_lane_s32(SINE(vgetq_lane_s32(phases, 3)), sine_vals, 3);
        
        // Add EG output
        int32x4_t table_idx = vaddq_s32(eg_out, sine_vals);
        
        // Store results (still need individual LogToLin)
        for (int j = 0; j < 4 && (i+j) < count; j++) {
            outputs[i+j] = ops[i+j].LogToLin(vgetq_lane_s32(table_idx, j));
            ops[i+j].out2_ = ops[i+j].out_;
            ops[i+j].out_ = outputs[i+j];
        }
    }
}
#endif

// Optimized Channel4::Calc with reduced branching
ISample Channel4::CalcOptimized()
{
    // Pre-calculate all operator outputs to improve pipelining
    ISample op0_out = op[0].Out();
    ISample op1_out = op[1].Out();
    ISample op2_out = op[2].Out();
    
    // Use jump table for algorithm selection (compiler optimization)
    static const void* algo_table[] = {
        &&algo_0, &&algo_1, &&algo_2, &&algo_3,
        &&algo_4, &&algo_5, &&algo_6, &&algo_7
    };
    
    int r = 0;
    goto *algo_table[algo_];
    
algo_0:
    op[2].Calc(op1_out);
    op[1].Calc(op0_out);
    r = op[3].Calc(op[2].Out());
    op[0].CalcFB(fb);
    return r;
    
algo_1:
    op[2].Calc(op0_out + op1_out);
    op[1].Calc(0);
    r = op[3].Calc(op[2].Out());
    op[0].CalcFB(fb);
    return r;
    
algo_2:
    op[2].Calc(op1_out);
    op[1].Calc(0);
    r = op[3].Calc(op0_out + op[2].Out());
    op[0].CalcFB(fb);
    return r;
    
algo_3:
    op[2].Calc(0);
    op[1].Calc(op0_out);
    r = op[3].Calc(op[1].Out() + op[2].Out());
    op[0].CalcFB(fb);
    return r;
    
algo_4:
    op[2].Calc(0);
    r = op[1].Calc(op0_out);
    r += op[3].Calc(op[2].Out());
    op[0].CalcFB(fb);
    return r;
    
algo_5:
    r = op[2].Calc(op0_out);
    r += op[1].Calc(op0_out);
    r += op[3].Calc(op0_out);
    op[0].CalcFB(fb);
    return r;
    
algo_6:
    r = op[2].Calc(0);
    r += op[1].Calc(op0_out);
    r += op[3].Calc(0);
    op[0].CalcFB(fb);
    return r;
    
algo_7:
    r = op[2].Calc(0);
    r += op[1].Calc(0);
    r += op[3].Calc(0);
    r += op[0].CalcFB(fb);
    return r;
}

} // namespace FM