#include "core/SongDetector.h"

#include <algorithm>
#include <cmath>

namespace fp {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Fields that change buffer sizes or filter state; editing anything else (thresholds,
// band edges, ratio mode) applies without losing history.
bool structureDiffers(const SongParams& a, const SongParams& b) {
    return a.butterworth != b.butterworth || a.filterOrder != b.filterOrder || a.filterLowHz != b.filterLowHz ||
           a.filterHighHz != b.filterHighHz || a.windowLength != b.windowLength ||
           a.frequencyBins != b.frequencyBins || a.hopMs != b.hopMs || a.songDurationSec != b.songDurationSec;
}
}  // namespace

void SongDetector::configure(double fs, const SongParams& p) {
    const bool rebuild = !built_ || fs != fs_ || structureDiffers(p, p_);
    p_ = p;
    fs_ = fs;

    if (rebuild) {
        built_ = true;
        L_ = std::clamp(p_.windowLength, 16, 1 << 16);
        ring_.assign(2 * size_t(L_), 0.0f);
        head_ = filled_ = 0;
        win_.resize(L_);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < L_; ++i) {
            win_[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / L_);
            s1 += win_[i];
            s2 += win_[i] * win_[i];
        }
        s1_ = s1;
        enbw_ = L_ * s2 / (s1 * s1);
        nfft_ = nextPow2(std::max(L_, 2 * std::max(1, p_.frequencyBins)));
        fft_.resize(nfft_);
        hopN_ = std::max(1, int(std::lround(p_.hopMs * fs_ / 1000.0)));
        sinceHop_ = 0;
        const size_t cols = size_t(std::max(1L, std::lround(p_.songDurationSec * 1000.0 / std::max(0.1, p_.hopMs))));
        above_.assign(cols, 0);
        aboveHead_ = aboveOnes_ = 0;
        holdLeftMs_ = 0.0;
        st_ = SongState{};
        if (p_.butterworth)
            filter_.design(fs_, p_.filterLowHz, p_.filterHighHz, std::clamp(p_.filterOrder, 1, 20));
        else
            filter_ = ButterworthBandpass{};
    }

    const double df = fs_ / nfft_;
    const int nyq = nfft_ / 2;
    kLow1_ = std::clamp(int(std::floor(p_.nonBirdCutoffHz / df)), 0, nyq);
    kBird0_ = std::clamp(int(std::ceil(std::min(p_.birdLowHz, p_.birdHighHz) / df)), 0, nyq);
    kBird1_ = std::clamp(int(std::floor(std::max(p_.birdLowHz, p_.birdHighHz) / df)), kBird0_, nyq);
}

void SongDetector::reset() {
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    head_ = filled_ = sinceHop_ = 0;
    std::fill(above_.begin(), above_.end(), uint8_t(0));
    aboveHead_ = aboveOnes_ = 0;
    holdLeftMs_ = 0.0;
    filter_.reset();
    st_ = SongState{};
}

const SongState& SongDetector::process(const float* x, int n) {
    st_.updated = false;
    if (!p_.enabled || L_ == 0) {
        st_.active = st_.detected = false;
        return st_;
    }

    scratch_.resize(size_t(n));
    for (int i = 0; i < n; ++i) scratch_[i] = float(x[i] * p_.gain + p_.offset);
    if (filter_.valid()) filter_.process(scratch_.data(), n);

    for (int i = 0; i < n; ++i) {
        ring_[head_] = scratch_[i];
        ring_[head_ + L_] = scratch_[i];
        if (++head_ == L_) head_ = 0;
        if (filled_ < L_) ++filled_;
        if (++sinceHop_ >= hopN_) {
            sinceHop_ = 0;
            if (filled_ == L_) computeColumn();
        }
    }

    const double dtMs = n * 1000.0 / fs_;
    if (st_.detected)
        holdLeftMs_ = p_.holdSec * 1000.0;
    else
        holdLeftMs_ = std::max(0.0, holdLeftMs_ - dtMs);
    st_.active = st_.detected || holdLeftMs_ > 0.0;
    return st_;
}

void SongDetector::computeColumn() {
    const float* w = &ring_[head_];  // oldest first
    double mean = 0.0;
    if (p_.meanSubtract) {
        for (int i = 0; i < L_; ++i) mean += w[i];
        mean /= L_;
    }
    for (int i = 0; i < L_; ++i) fft_[i] = {(w[i] - mean) * win_[i], 0.0};
    std::fill(fft_.begin() + L_, fft_.end(), std::complex<double>(0.0, 0.0));  // zero-pad
    fftInPlace(fft_);

    // Single-sided power per bin in Vrms^2 (same scaling as BandEnvelope).
    const double norm = 2.0 / (s1_ * s1_);
    auto power = [&](int k) { return std::norm(fft_[k]) * norm; };
    double low = 0.0, bird = 0.0;
    for (int k = 0; k <= kLow1_; ++k) low += power(k);
    for (int k = kBird0_; k <= kBird1_; ++k) bird += power(k);
    const double lowMean = low / (kLow1_ + 1);
    const double birdMean = bird / (kBird1_ - kBird0_ + 1);

    double ratio;
    if (p_.ratioMode == SongRatioMode::BirdOverTotal)
        ratio = birdMean / std::max(birdMean + lowMean, 1e-30);
    else
        ratio = birdMean / std::max(lowMean, 1e-30);
    st_.ratio = ratio;
    st_.birdRms = std::sqrt(bird / enbw_);
    st_.above = ratio > p_.ratioThresh && st_.birdRms >= p_.minBirdRms;

    // Box-kernel convolution, evaluated at the newest column.
    aboveOnes_ -= above_[aboveHead_];
    above_[aboveHead_] = st_.above ? 1 : 0;
    aboveOnes_ += above_[aboveHead_];
    aboveHead_ = (aboveHead_ + 1) % above_.size();
    st_.fraction = double(aboveOnes_) / double(above_.size());
    st_.detected = st_.fraction > p_.durationThresh;
    st_.updated = true;
}

}  // namespace fp
