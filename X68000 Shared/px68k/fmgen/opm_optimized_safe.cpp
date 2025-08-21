// Conservative optimized version of OPM mixing functions
// This file is included from opm.cpp

// Fixed version of MixSub  
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

// SIMD implementations disabled for audio quality assurance
// They can be re-enabled after thorough testing

// End of optimized implementations