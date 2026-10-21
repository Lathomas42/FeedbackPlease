#include "core/SynthSong.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace fp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

std::vector<SynthSyllable> defaultMotif() {
    // The detector's offset lags the true syllable end by most of the 15 ms window,
    // so measured durations come out ~10 ms longer than durMs below.
    return {
        {50.0, 40.0, 650.0},
        {60.0, 35.0, 900.0},
        {83.0, 41.0, 550.0},
        {100.0, 50.0, 750.0},
        {70.0, 60.0, 1100.0},
    };
}

void addSyllable(std::vector<float>& y, double fs, double startMs, double durMs, double f0Hz, double amplitude) {
    const size_t a = size_t(startMs * fs / 1000.0);
    const size_t len = size_t(durMs * fs / 1000.0);
    if (a + len >= y.size()) return;
    const size_t ramp = size_t(5.0 * fs / 1000.0);
    for (size_t i = 0; i < len; ++i) {
        const double t = i / fs;
        double env = 1.0;
        if (i < ramp) env = 0.5 - 0.5 * std::cos(kPi * i / ramp);
        if (len - 1 - i < ramp) env = std::min(env, 0.5 - 0.5 * std::cos(kPi * (len - 1 - i) / ramp));
        // Harmonic stack with a slight downward sweep; a broad "formant" around 4 kHz
        // puts most energy in the 2-7 kHz range, like zebra finch song.
        const double f0 = f0Hz * (1.0 - 0.1 * t / (durMs / 1000.0));
        double v = 0.0;
        for (int h = 1; h * f0Hz < 9000.0; ++h) {
            const double fh = h * f0Hz;
            const double w = std::exp(-std::pow((fh - 4000.0) / 2500.0, 2.0));
            v += w * std::sin(2.0 * kPi * f0 * h * t);
        }
        y[a + i] += float(amplitude * 0.3 * env * v);
    }
}

std::vector<float> synthesizeSong(double fs, double seconds, double noiseVolts, uint32_t seed,
                                  const std::vector<SynthSyllable>& motif, double motifGapMs, double amplitude,
                                  double cageNoiseVolts) {
    const size_t n = size_t(seconds * fs);
    std::vector<float> y(n, 0.0f);
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, 1.0);

    // Low-frequency cage noise: white noise through four one-pole lowpasses at 300 Hz
    // (steep enough to stay out of the 1-7 kHz detection band), scaled to cageNoiseVolts RMS.
    std::vector<double> cage(n);
    const double a = std::exp(-2.0 * kPi * 300.0 / fs);
    double s[4] = {0, 0, 0, 0}, sumSq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double v = g(rng);
        for (double& st : s) v = st = a * st + (1.0 - a) * v;
        cage[i] = v;
        sumSq += v * v;
    }
    const double cageScale = n > 0 && sumSq > 0 ? cageNoiseVolts / std::sqrt(sumSq / n) : 0.0;
    for (size_t i = 0; i < n; ++i) y[i] = float(noiseVolts * g(rng) + cageScale * cage[i]);

    if (motif.empty()) return y;  // noise only
    double t0 = 200.0;  // ms
    while (true) {
        for (const auto& s : motif) {
            if ((t0 + s.durMs) * fs / 1000.0 >= double(n)) return y;
            addSyllable(y, fs, t0, s.durMs, s.f0Hz, amplitude);
            t0 += s.durMs + s.gapMs;
        }
        t0 += motifGapMs;
    }
}

}  // namespace fp
