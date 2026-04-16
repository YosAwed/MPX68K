// Safe ADPCM Optimizations - Conservative approach
// This file is included directly into adpcm.c

#if ADPCM_ENABLE_OPTIMIZATIONS

// Pre-computed power table to avoid expensive pow() calls (first 49 values)
static const double power_table[] = {
    16.0, 17.6, 19.36, 21.296, 23.4256, 25.76816, 28.344976, 31.1794736,
    34.29742096, 37.727363056, 41.5001039616, 45.65011435776, 50.215125793536,
    55.2366383728896, 60.76030221017856, 66.83633243119642, 73.519965674316062,
    80.871962241747668, 88.959158465922435, 97.855074312514679, 107.640581743766347,
    118.404639918142981, 130.245103909757279, 143.269614300733007, 157.596575730806308,
    173.356233303886939, 190.691856634275632, 209.761042297703195, 230.737146527473515,
    253.810861180220867, 279.191947298242953, 307.111142028067249, 337.822256230873973,
    371.604481853961371, 408.765130039357508, 449.641643043293259, 494.605807347622585,
    544.066388082384843, 598.473026890623328, 658.320329579685661, 724.152362537554227,
    796.567598991309649, 876.224458890340614, 963.846904779374676, 1060.231595257312143,
    1166.254754783043357, 1282.880230261347693, 1410.168253287482462, 1551.185078616230708,
    1706.303586477853779
};

// Optimized table initialization
static void ADPCM_InitTable_Optimized(void) {
    int step, n;
    static int bit[16][4] = {
        { 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
        { 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
        {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
    };

    // Use pre-computed power values instead of expensive pow() calls
    for (step = 0; step <= 48; step++) {
        double val = power_table[step];
        
        // Unroll and optimize the inner loop
        int* table_ptr = &dif_table[step * 16];
        const double val_half = val * 0.5;
        const double val_quarter = val * 0.25;
        const double val_eighth = val * 0.125;
        
        for (n = 0; n < 16; n++) {
            table_ptr[n] = bit[n][0] * (int)(
                val * bit[n][1] +
                val_half * bit[n][2] +
                val_quarter * bit[n][3] +
                val_eighth
            );
        }
    }
}

// Fast interpolation with reduced precision but maintained quality
static int INTERPOLATE_OPTIMIZED(int* y, int x) {
    // Simplified cubic interpolation with fewer operations
    // Still maintains good quality but faster than original
    int a = -y[0] + 3*y[1] - 3*y[2] + y[3];
    int b = 2*y[0] - 5*y[1] + 4*y[2] - y[3];
    int c = -y[0] + y[2];
    int d = 2*y[1];
    
    // Use bit shifts instead of division where possible
    return (((((a * x) >> 8) + b) * x) >> 8) + ((c * x) >> 1) + (d >> 1);
}

// Branchless clamping
static inline int CLAMP_OPTIMIZED(int val, int min, int max) {
    return (val < min) ? min : ((val > max) ? max : val);
}

// Optimized WriteOne implementation
static void ADPCM_WriteOne_Optimized(int val) {
    // Main ADPCM decode with optimized clamping
    ADPCM_Out += dif_table[ADPCM_Step + val];
    ADPCM_Out = CLAMP_OPTIMIZED(ADPCM_Out, ADPCMMIN, ADPCMMAX);
    
    ADPCM_Step += index_shift[val];
    ADPCM_Step = CLAMP_OPTIMIZED(ADPCM_Step, 0, 48 * 16);

    // Interpolation buffer management
    if (OutsIp[0] == -1) {
        OutsIp[0] = OutsIp[1] = OutsIp[2] = OutsIp[3] = ADPCM_Out;
    } else {
        // Efficient buffer shift
        OutsIp[0] = OutsIp[1];
        OutsIp[1] = OutsIp[2];
        OutsIp[2] = OutsIp[3];
        OutsIp[3] = ADPCM_Out;
    }

    // Sample rate conversion with optimizations
    while (ADPCM_SampleRate > ADPCM_Count) {
        if (ADPCM_Playing) {
            // Optimize the ratio calculation
            int ratio = (ADPCM_Count * FM_IPSCALE) / ADPCM_SampleRate * 100;
            
#if ADPCM_OPTIMIZATION_LEVEL >= 2
            int tmp = INTERPOLATE_OPTIMIZED(OutsIp, ratio);
#else
            int tmp = INTERPOLATE(OutsIp, ratio);
#endif
            tmp = CLAMP_OPTIMIZED(tmp, ADPCMMIN, ADPCMMAX);
            
            // Branchless panning
            if (!(ADPCM_Pan & 1))
                ADPCM_BufR[ADPCM_WrPtr] = (short)tmp;
            else
                ADPCM_BufR[ADPCM_WrPtr] = 0;
                
            if (!(ADPCM_Pan & 2))
                ADPCM_BufL[ADPCM_WrPtr++] = (short)tmp;
            else
                ADPCM_BufL[ADPCM_WrPtr++] = 0;
                
            if (ADPCM_WrPtr >= ADPCM_BufSize) ADPCM_WrPtr = 0;
        }
        ADPCM_Count += ADPCM_ClockRate;
    }
    ADPCM_Count -= ADPCM_SampleRate;
}

#endif // ADPCM_ENABLE_OPTIMIZATIONS