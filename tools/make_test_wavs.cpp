// Writes a folder of synthetic "recordings" for trying out the Tune tab:
// bouts of the default motif with per-file tempo jitter, level changes, cage
// noise, isolated calls, and some files with no song at all.
//
//   make_test_wavs <out_dir> [count=12]

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

#include "core/SynthSong.h"
#include "core/WavFile.h"

using namespace fp;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <out_dir> [count]\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const int count = argc > 2 ? std::atoi(argv[2]) : 12;
    std::filesystem::create_directories(dir);
    const double fs = 40000.0;
    std::mt19937 rng(7);
    std::normal_distribution<double> jitter(1.0, 0.03);
    std::uniform_real_distribution<double> u(0.0, 1.0);

    for (int i = 0; i < count; ++i) {
        const double seconds = 4.0 + 6.0 * u(rng);
        const bool noSong = i % 5 == 4;  // every 5th file: calls and cage noise only
        auto motif = defaultMotif();
        const double tempo = jitter(rng);  // whole-file tempo change, like day-to-day variation
        for (auto& s : motif) {
            s.durMs *= tempo;
            s.gapMs *= tempo;
        }
        const double level = 0.1 + 0.25 * u(rng);
        auto y = noSong ? synthesizeSong(fs, seconds, 0.0005, uint32_t(100 + i), {}, 600, level, 0.004 + 0.01 * u(rng))
                        : synthesizeSong(fs, seconds, 0.0005, uint32_t(100 + i), motif, 500 + 400 * u(rng), level,
                                         0.002 + 0.006 * u(rng));
        // A few isolated calls between bouts.
        const int calls = 1 + int(3 * u(rng));
        for (int c = 0; c < calls; ++c)
            addSyllable(y, fs, 300 + (seconds * 1000 - 600) * u(rng), 30 + 30 * u(rng), 500 + 600 * u(rng), level);

        char name[64];
        std::snprintf(name, sizeof name, "/bird_test_%02d%s.wav", i + 1, noSong ? "_nosong" : "");
        WavWriter w;
        if (!w.open(dir + name, fs)) {
            std::fprintf(stderr, "cannot write %s%s\n", dir.c_str(), name);
            return 1;
        }
        w.write(y.data(), y.size());
        w.close();
        std::printf("%s%s  %.1f s\n", dir.c_str(), name, seconds);
    }
    return 0;
}
