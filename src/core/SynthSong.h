#pragma once
// Synthetic zebra-finch-like song for testing without a bird or a DAQ card:
// a repeating motif of harmonic-stack syllables in light background noise.

#include <cstdint>
#include <vector>

namespace fp {

struct SynthSyllable {
    double durMs;
    double gapMs;  // silence after the syllable; IOI to the next syllable = durMs + gapMs
    double f0Hz;
};

// Default motif. The 2nd -> 3rd -> 4th syllables have IOIs of 95 and 124 ms, matching
// the default "IOI, SD, IOI" setpoints so the simulator produces feedback.
std::vector<SynthSyllable> defaultMotif();

// noiseVolts: white microphone noise. cageNoiseVolts: low-frequency (<~500 Hz) noise
// like fans and cage rattle, which the song detector's non-bird band picks up.
std::vector<float> synthesizeSong(double sampleRate, double seconds, double noiseVolts, uint32_t seed = 1,
                                  const std::vector<SynthSyllable>& motif = defaultMotif(),
                                  double motifGapMs = 600.0, double amplitude = 0.2, double cageNoiseVolts = 0.003);

// Adds one harmonic-stack syllable (same timbre as the song) to y.
void addSyllable(std::vector<float>& y, double sampleRate, double startMs, double durMs, double f0Hz,
                 double amplitude = 0.2);

}  // namespace fp
