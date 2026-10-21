#pragma once
// Port of DetectBirdSong.vi + its "STFT Spectrogram Record" subVI, made streaming.
//
// Every hopMs a spectrogram column is computed from the last windowLength samples.
// For that column:
//   birdPower = mean power over bins in [birdLowHz, birdHighHz]
//   lowPower  = mean power over bins in [0, nonBirdCutoffHz]
//   above     = ratio(birdPower, lowPower) > ratioThresh
// The 0/1 "above" trace is averaged over the last songDurationSec (the LabVIEW
// convolution with a box kernel), and song is detected when that fraction exceeds
// durationThresh.

#include <complex>
#include <cstdint>
#include <vector>

#include "core/Filters.h"
#include "core/Params.h"

namespace fp {

struct SongState {
    bool updated = false;   // a new spectrogram column was computed this frame
    double ratio = 0.0;     // power ratio of the latest column
    double birdRms = 0.0;   // RMS in the bird band of the latest column (V)
    bool above = false;     // latest column above the ratio threshold
    double fraction = 0.0;  // "Convolved Thingy": share of columns above threshold in the kernel
    bool detected = false;  // fraction > durationThresh ("bDetect?")
    bool active = false;    // detected, extended by holdSec
};

class SongDetector {
public:
    void configure(double sampleRate, const SongParams& p);
    void reset();
    const SongState& process(const float* x, int n);
    const SongState& state() const { return st_; }

    int fftSize() const { return nfft_; }
    double binHz() const { return nfft_ > 0 ? fs_ / nfft_ : 0.0; }

private:
    void computeColumn();

    SongParams p_;
    double fs_ = 0.0;
    bool built_ = false;

    ButterworthBandpass filter_;
    std::vector<float> scratch_;

    int L_ = 0;                 // analysis window length
    std::vector<float> ring_;   // 2*L_, written twice so the window is contiguous
    int head_ = 0, filled_ = 0;
    std::vector<double> win_;
    double s1_ = 1.0, enbw_ = 1.0;

    int nfft_ = 0;
    std::vector<std::complex<double>> fft_;
    int hopN_ = 1, sinceHop_ = 0;
    int kLow1_ = 0, kBird0_ = 0, kBird1_ = 0;

    std::vector<uint8_t> above_;  // ring of the last N column decisions
    size_t aboveHead_ = 0, aboveOnes_ = 0;

    double holdLeftMs_ = 0.0;
    SongState st_;
};

}  // namespace fp
