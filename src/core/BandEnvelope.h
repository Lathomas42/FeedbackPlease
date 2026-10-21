#pragma once
// Port of the "Compute Power Spectrum" + "Spectrum to Amp Multibands" part of
// DetectSyllables.vi. Takes the last windowMs of audio and returns one number:
// the weighted band amplitude of the include bands combined with the subtract bands.

#include <vector>

#include "core/Params.h"

namespace fp {

class BandEnvelope {
public:
    struct Result {
        double value = 0.0;        // combined detection signal (linear)
        double includeAmp = 0.0;   // sum of gain * band RMS over include bands
        double subtractAmp = 0.0;  // sum of gain * band RMS over subtract bands
    };

    void configure(double sampleRate, int windowSamples, const SpectrumParams& sp, const ProcessingParams& pp);

    // window must point at windowSamples() samples, oldest first.
    Result compute(const float* window);

    int windowSamples() const { return n_; }
    double binWidthHz() const { return n_ > 0 ? fs_ / n_ : 0.0; }

private:
    struct BandBins {
        double gain;
        int k0, k1;  // inclusive DFT bin range
        bool include;
    };

    double fs_ = 0.0;
    int n_ = 0;
    ProcessingParams pp_;
    BandCombination combination_ = BandCombination::Difference;

    std::vector<double> win_;   // Hann window
    std::vector<double> work_;  // windowed samples
    std::vector<BandBins> bands_;
    std::vector<int> bins_;          // distinct bins to evaluate
    std::vector<int> binSlot_;       // bin k -> index into bins_, or -1
    std::vector<double> binCos_, binSin_, binCoeff_;
    std::vector<double> gs1_, gs2_;  // Goertzel state, one per bin
    std::vector<double> binPower_;   // single-sided power per bin, Vrms^2
    double s1_ = 1.0;    // sum of window
    double enbw_ = 1.0;  // equivalent noise bandwidth, in bins
};

}  // namespace fp
