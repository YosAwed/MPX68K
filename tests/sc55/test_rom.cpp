// Optional integration check using a caller-supplied ROM folder. Never include ROMs here.
#include "../../X68000 Shared/SC55/SC55Bridge.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Metrics { double acRMS; double peak; };
Metrics render(int blocks)
{
    float left[1024], right[1024];
    double sum = 0, squares = 0, peak = 0;
    for (int block = 0; block < blocks; ++block) {
        if (!X68SC55_Render(left, right, 1024)) throw std::runtime_error("render failed");
        for (int frame = 0; frame < 1024; ++frame) {
            for (float sample : {left[frame], right[frame]}) {
                if (!std::isfinite(sample)) throw std::runtime_error("nonfinite audio");
                sum += sample;
                squares += double(sample) * sample;
                peak = std::max(peak, std::abs(double(sample)));
            }
        }
    }
    const double count = blocks * 2048.0, mean = sum / count;
    return {std::sqrt(std::max(0.0, squares / count - mean * mean)), peak};
}

int main(int argc, char **argv)
{
    try {
        if (argc != 2) throw std::runtime_error("usage: test-rom ROM_FOLDER");
        std::vector<std::vector<uint8_t>> roms;
        for (const char *name : {"sc55_rom1.bin", "sc55_rom2.bin", "sc55_waverom1.bin", "sc55_waverom2.bin", "sc55_waverom3.bin"}) {
            std::ifstream file(std::string(argv[1]) + "/" + name, std::ios::binary | std::ios::ate);
            const auto size = file.tellg();
            if (!file || size <= 0 || size > 0x100000) throw std::runtime_error(name);
            roms.emplace_back(static_cast<size_t>(size));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char *>(roms.back().data()), size)) throw std::runtime_error(name);
        }
        if (roms[3].size() != roms[2].size() || roms[4].size() != roms[2].size() ||
            !X68SC55_Load(roms[0].data(), roms[0].size(), roms[1].data(), roms[1].size(),
                         roms[2].data(), roms[3].data(), roms[4].data(), roms[2].size()))
            throw std::runtime_error("invalid ROM sizes");
        double firstNote = 0;
        for (int pass = 0; pass < 2; ++pass) {
            if (pass) X68SC55_Reset();
            render(250); // Four seconds: matches the host firmware warmup.
            const auto idle = render(8);
            const uint8_t on[] = {0x90, 60, 100}, off[] = {0x80, 60, 0};
            if (!X68SC55_Send(on, sizeof(on))) throw std::runtime_error("note-on rejected");
            const auto note = render(38);
            if (!X68SC55_Send(off, sizeof(off))) throw std::runtime_error("note-off rejected");
            render(125);
            const auto tail = render(63);
            std::printf("pass %d: idle AC RMS %.7f, note %.7f, tail %.7f, peak %.7f\n",
                        pass + 1, idle.acRMS, note.acRMS, tail.acRMS, note.peak);
            if (note.acRMS < idle.acRMS * 4 + 0.001 || tail.acRMS > note.acRMS / 4)
                throw std::runtime_error("note-on/off did not produce the expected audio change");
            if (pass && std::abs(firstNote - note.acRMS) > 0.000001)
                throw std::runtime_error("reset did not reproduce note output");
            firstNote = note.acRMS;
        }
        std::puts("SC-55 real-ROM note-on/off and reset checks passed");
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
