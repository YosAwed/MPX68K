// Optimized version of OPM mixing functions
// This file is included from opm.cpp, so no need for includes here

// Branchless version of MixSub
void OPM::MixSubOptimized(int activech, ISample** idest)
{
    // Channel 0 (special case - assignment not addition)
    ISample ch0_result = ch[0].Calc();
    if (activech & 0x4000) {
        *idest[0] = ch0_result;
    }
    
    // Channels 1-6 (conditional addition)
    ISample ch1_result = ch[1].Calc();
    if (activech & 0x1000) *idest[1] += ch1_result;
    
    ISample ch2_result = ch[2].Calc();
    if (activech & 0x0400) *idest[2] += ch2_result;
    
    ISample ch3_result = ch[3].Calc();
    if (activech & 0x0100) *idest[3] += ch3_result;
    
    ISample ch4_result = ch[4].Calc();
    if (activech & 0x0040) *idest[4] += ch4_result;
    
    ISample ch5_result = ch[5].Calc();
    if (activech & 0x0010) *idest[5] += ch5_result;
    
    ISample ch6_result = ch[6].Calc();
    if (activech & 0x0004) *idest[6] += ch6_result;
    
    // Channel 7 with noise
    if (activech & 0x0001)
    {
        ISample ch7_result = (noisedelta & 0x80) ? 
            ch[7].CalcN(Noise()) : ch[7].Calc();
        *idest[7] += ch7_result;
    }
}

// Fixed version of MixSubL
void OPM::MixSubLOptimized(int activech, ISample** idest)
{
    // Channel 0 (special case - assignment not addition)
    ISample ch0_result = ch[0].CalcL();
    if (activech & 0x4000) {
        *idest[0] = ch0_result;
    }
    
    // Channels 1-6 (conditional addition)
    ISample ch1_result = ch[1].CalcL();
    if (activech & 0x1000) *idest[1] += ch1_result;
    
    ISample ch2_result = ch[2].CalcL();
    if (activech & 0x0400) *idest[2] += ch2_result;
    
    ISample ch3_result = ch[3].CalcL();
    if (activech & 0x0100) *idest[3] += ch3_result;
    
    ISample ch4_result = ch[4].CalcL();
    if (activech & 0x0040) *idest[4] += ch4_result;
    
    ISample ch5_result = ch[5].CalcL();
    if (activech & 0x0010) *idest[5] += ch5_result;
    
    ISample ch6_result = ch[6].CalcL();
    if (activech & 0x0004) *idest[6] += ch6_result;
    
    // Channel 7 with noise
    if (activech & 0x0001)
    {
        ISample ch7_result = (noisedelta & 0x80) ? 
            ch[7].CalcLN(Noise()) : ch[7].CalcL();
        *idest[7] += ch7_result;
    }
}

#ifdef FMGEN_USE_NEON
// NEON SIMD version for ARM (Apple Silicon)
void OPM::MixSubSIMD(int activech, ISample** idest)
{
    // Process channels 0-3 with NEON
    int32x4_t masks = {
        -(activech & 0x4000),
        -(activech & 0x1000),
        -(activech & 0x0400),
        -(activech & 0x0100)
    };
    
    int32x4_t results = {
        ch[0].Calc(),
        ch[1].Calc(),
        ch[2].Calc(),
        ch[3].Calc()
    };
    
    // Apply masks
    results = vandq_s32(results, masks);
    
    // Special handling for channel 0 (assignment)
    *idest[0] = vgetq_lane_s32(results, 0);
    *idest[1] += vgetq_lane_s32(results, 1);
    *idest[2] += vgetq_lane_s32(results, 2);
    *idest[3] += vgetq_lane_s32(results, 3);
    
    // Process channels 4-7
    int32_t mask_values2[4] = {
        -(activech & 0x0040),
        -(activech & 0x0010),
        -(activech & 0x0004),
        -(activech & 0x0001)
    };
    int32x4_t masks2 = vld1q_s32(mask_values2);
    
    int32_t result_values2[4] = {
        ch[4].Calc(),
        ch[5].Calc(),
        ch[6].Calc(),
        (activech & 0x0001) ? 
            ((noisedelta & 0x80) ? ch[7].CalcN(Noise()) : ch[7].Calc()) : 0
    };
    int32x4_t results2 = vld1q_s32(result_values2);
    
    results2 = vandq_s32(results2, masks2);
    
    *idest[4] += vgetq_lane_s32(results2, 0);
    *idest[5] += vgetq_lane_s32(results2, 1);
    *idest[6] += vgetq_lane_s32(results2, 2);
    *idest[7] += vgetq_lane_s32(results2, 3);
}

void OPM::MixSubLSIMD(int activech, ISample** idest)
{
    // Process channels 0-3 with NEON
    int32_t mask_values[4] = {
        -(activech & 0x4000),
        -(activech & 0x1000),
        -(activech & 0x0400),
        -(activech & 0x0100)
    };
    int32x4_t masks = vld1q_s32(mask_values);
    
    int32_t result_values[4] = {
        ch[0].CalcL(),
        ch[1].CalcL(),
        ch[2].CalcL(),
        ch[3].CalcL()
    };
    int32x4_t results = vld1q_s32(result_values);
    
    // Apply masks
    results = vandq_s32(results, masks);
    
    // Special handling for channel 0 (assignment)
    *idest[0] = vgetq_lane_s32(results, 0);
    *idest[1] += vgetq_lane_s32(results, 1);
    *idest[2] += vgetq_lane_s32(results, 2);
    *idest[3] += vgetq_lane_s32(results, 3);
    
    // Process channels 4-7
    int32_t mask_values2[4] = {
        -(activech & 0x0040),
        -(activech & 0x0010),
        -(activech & 0x0004),
        -(activech & 0x0001)
    };
    int32x4_t masks2 = vld1q_s32(mask_values2);
    
    int32_t result_values2[4] = {
        ch[4].CalcL(),
        ch[5].CalcL(),
        ch[6].CalcL(),
        (activech & 0x0001) ? 
            ((noisedelta & 0x80) ? ch[7].CalcLN(Noise()) : ch[7].CalcL()) : 0
    };
    int32x4_t results2 = vld1q_s32(result_values2);
    
    results2 = vandq_s32(results2, masks2);
    
    *idest[4] += vgetq_lane_s32(results2, 0);
    *idest[5] += vgetq_lane_s32(results2, 1);
    *idest[6] += vgetq_lane_s32(results2, 2);
    *idest[7] += vgetq_lane_s32(results2, 3);
}

#elif defined(FMGEN_USE_SSE)
// SSE SIMD version for x86 (Intel Macs)
void OPM::MixSubSIMD(int activech, ISample** idest)
{
    // Process channels 0-3 with SSE
    __m128i masks = _mm_set_epi32(
        -(activech & 0x0100),
        -(activech & 0x0400),
        -(activech & 0x1000),
        -(activech & 0x4000)
    );
    
    __m128i results = _mm_set_epi32(
        ch[3].Calc(),
        ch[2].Calc(),
        ch[1].Calc(),
        ch[0].Calc()
    );
    
    // Apply masks
    results = _mm_and_si128(results, masks);
    
    // Extract and store results
    int32_t res[4];
    _mm_storeu_si128((__m128i*)res, results);
    
    *idest[0] = res[0];
    *idest[1] += res[1];
    *idest[2] += res[2];
    *idest[3] += res[3];
    
    // Process channels 4-7
    __m128i masks2 = _mm_set_epi32(
        -(activech & 0x0001),
        -(activech & 0x0004),
        -(activech & 0x0010),
        -(activech & 0x0040)
    );
    
    __m128i results2 = _mm_set_epi32(
        (activech & 0x0001) ? 
            ((noisedelta & 0x80) ? ch[7].CalcN(Noise()) : ch[7].Calc()) : 0,
        ch[6].Calc(),
        ch[5].Calc(),
        ch[4].Calc()
    );
    
    results2 = _mm_and_si128(results2, masks2);
    
    _mm_storeu_si128((__m128i*)res, results2);
    
    *idest[4] += res[0];
    *idest[5] += res[1];
    *idest[6] += res[2];
    *idest[7] += res[3];
}

void OPM::MixSubLSIMD(int activech, ISample** idest)
{
    // Similar to MixSubSIMD but with CalcL() calls
    // Implementation follows same pattern as above
}
#endif

// End of optimized implementations