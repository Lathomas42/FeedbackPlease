#include "core/BandEnvelope.h"

#include <algorithm>
#include <cmath>

namespace fp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void BandEnvelope::configure(double sampleRate, int windowSamples, const SpectrumParams& sp,
                             const ProcessingParams& pp) {
    fs_ = sampleRate;
    pp_ = pp;
    combination_ = sp.combination;
    if (n_ != windowSamples) {
        n_ = windowSamples;
        win_.resize(n_);
        work_.resize(n_);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < n_; ++i) {
            // Periodic Hann, matching LabVIEW's default window for its power spectrum VIs.
            win_[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n_);
            s1 += win_[i];
            s2 += win_[i] * win_[i];
        }
        s1_ = s1;
        enbw_ = n_ * s2 / (s1 * s1);
    }

    const int nyquistBin = n_ / 2;
    const double df = fs_ / n_;
    bands_.clear();
    auto addBands = [&](const std::array<Band, kBandsPerGroup>& group, bool include) {
        for (const Band& b : group) {
            if (b.gain == 0.0) continue;
            double lo = std::min(b.lowHz, b.highHz), hi = std::max(b.lowHz, b.highHz);
            int k0 = int(std::ceil(lo / df));
            int k1 = int(std::floor(hi / df));
            k0 = std::clamp(k0, 0, nyquistBin);
            k1 = std::clamp(k1, 0, nyquistBin);
            if (k1 < k0) k0 = k1 = std::clamp(int(std::lround(0.5 * (lo + hi) / df)), 0, nyquistBin);  // narrower than one bin
            bands_.push_back({b.gain, k0, k1, include});
        }
    };
    addBands(sp.include, true);
    addBands(sp.subtract, false);

    binSlot_.assign(nyquistBin + 1, -1);
    bins_.clear();
    for (const auto& b : bands_)
        for (int k = b.k0; k <= b.k1; ++k)
            if (binSlot_[k] < 0) {
                binSlot_[k] = int(bins_.size());
                bins_.push_back(k);
            }
    binCos_.resize(bins_.size());
    binSin_.resize(bins_.size());
    binPower_.assign(bins_.size(), 0.0);
    binCoeff_.resize(bins_.size());
    gs1_.assign(bins_.size(), 0.0);
    gs2_.assign(bins_.size(), 0.0);
    for (size_t j = 0; j < bins_.size(); ++j) {
        const double w = 2.0 * kPi * bins_[j] / n_;
        binCos_[j] = std::cos(w);
        binSin_[j] = std::sin(w);
        binCoeff_[j] = 2.0 * binCos_[j];
    }
}

BandEnvelope::Result BandEnvelope::compute(const float* x) {
    Result r;
    if (n_ <= 0) return r;

    double mean = 0.0;
    if (pp_.meanSubtract) {
        for (int i = 0; i < n_; ++i) mean += x[i];
        mean /= n_;
    }
    const double g = pp_.softwareGain ? pp_.gain : 1.0;
    for (int i = 0; i < n_; ++i) work_[i] = (x[i] - mean) * g * win_[i];

    // Goertzel per needed bin: only the bins inside enabled bands are computed,
    // which is cheaper than a full FFT for a few hundred bins at 1 kHz. The bins are
    // updated side by side (inner loop over bins) so the compiler can vectorize and
    // the per-bin recursions don't stall on each other's latency.
    const double norm = 1.0 / (s1_ * s1_);
    const size_t nb = bins_.size();
    std::fill(gs1_.begin(), gs1_.end(), 0.0);
    std::fill(gs2_.begin(), gs2_.end(), 0.0);
    double* s1v = gs1_.data();
    double* s2v = gs2_.data();
    const double* cf = binCoeff_.data();
    for (int i = 0; i < n_; ++i) {
        const double xi = work_[i];
        for (size_t j = 0; j < nb; ++j) {
            const double s = xi + cf[j] * s1v[j] - s2v[j];
            s2v[j] = s1v[j];
            s1v[j] = s;
        }
    }
    for (size_t j = 0; j < nb; ++j) {
        const double c = binCos_[j];
        const double s1 = s1v[j], s2 = s2v[j];
        const double re = s1 - c * s2;
        const double im = binSin_[j] * s2;
        const int k = bins_[j];
        const double sided = (k == 0 || 2 * k == n_) ? 1.0 : 2.0;
        binPower_[j] = sided * (re * re + im * im) * norm;  // Vrms^2 of a sine centred on bin k
    }

    for (const auto& b : bands_) {
        double p = 0.0;
        for (int k = b.k0; k <= b.k1; ++k) p += binPower_[binSlot_[k]];
        // Dividing by ENBW turns summed windowed bins into the RMS power in the band.
        const double amp = std::sqrt(p / enbw_);
        (b.include ? r.includeAmp : r.subtractAmp) += b.gain * amp;
    }

    switch (combination_) {
        case BandCombination::Difference: r.value = r.includeAmp - r.subtractAmp; break;
        case BandCombination::Sum: r.value = r.includeAmp + r.subtractAmp; break;
        case BandCombination::Ratio: r.value = r.includeAmp / std::max(r.subtractAmp, 1e-12); break;
        case BandCombination::IncludeOnly: r.value = r.includeAmp; break;
    }
    if (pp_.lowCutoff && r.value < pp_.lowCutoffThresh) r.value = 0.0;
    return r;
}

}  // namespace fp
