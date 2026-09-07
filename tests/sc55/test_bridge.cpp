// Synthetic firmware only: no Roland ROM files are needed or included.
#include "../../X68000 Shared/SC55/SC55Bridge.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    std::array<float, 1024> left{}, right{}, first{};
    assert(!X68SC55_Render(left.data(), right.data(), left.size()));
    assert(!X68SC55_Render(nullptr, right.data(), left.size()));
    std::vector<uint8_t> rom1(0x8000), rom2(0x40000), wave(0x100000);
    // Reset vector 0x100, followed by H8 SLEEP.
    rom1[2] = 1;
    rom1[0x100] = 0x1a;
    auto load = [&](size_t programSize, size_t waveSize) {
        return X68SC55_Load(rom1.data(), programSize, rom2.data(), rom2.size(),
                           wave.data(), wave.data(), wave.data(), waveSize);
    };
    assert(!load(0x7fff, wave.size()));
    assert(!load(rom1.size(), 0xfffff));
    assert(load(rom1.size(), wave.size()));
    assert(X68SC55_Render(left.data(), right.data(), left.size()));
    first = left;
    for (auto sample : left) assert(std::isfinite(sample) && std::abs(sample) <= 1);
    // Reset must clear every timing domain and reproduce the same first samples.
    for (int n = 0; n < 5; ++n) {
        assert(X68SC55_Render(left.data(), right.data(), left.size()));
        X68SC55_Reset();
        assert(X68SC55_Render(left.data(), right.data(), left.size()));
        assert(left == first);
    }
    // A full UART must reject input instead of wrapping and overwriting it.
    std::vector<uint8_t> midi(8191, 0xfe);
    assert(X68SC55_Send(midi.data(), midi.size()));
    assert(!X68SC55_Send(midi.data(), 1));
    X68SC55_Reset();
    assert(X68SC55_Send(midi.data(), 1));
    assert(!X68SC55_Send(nullptr, 1));
    assert(load(rom1.size(), wave.size()));
    assert(X68SC55_Send(midi.data(), midi.size()));
    puts("SC-55 bridge checks passed");
}
