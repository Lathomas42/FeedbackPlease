#include "core/Filters.h"

#include <algorithm>
#include <cmath>

namespace fp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

int nextPow2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

void fftInPlace(std::vector<std::complex<double>>& x) {
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / double(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = x[i + k];
                const auto v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

void ButterworthBandpass::design(double fs, double lowHz, double highHz, int order) {
    sos_.clear();
    fs_ = fs;
    const double nyq = fs / 2.0;
    double f1 = std::min(lowHz, highHz), f2 = std::max(lowHz, highHz);
    f2 = std::min(f2, nyq * 0.999);
    if (order < 1 || f1 <= 0.0 || f2 <= f1) return;

    // Pre-warped analog band edges, then lowpass prototype -> bandpass -> bilinear.
    const double w1 = 2.0 * fs * std::tan(kPi * f1 / fs);
    const double w2 = 2.0 * fs * std::tan(kPi * f2 / fs);
    const double w0sq = w1 * w2, bw = w2 - w1;
    using C = std::complex<double>;
    std::vector<C> zpoles;
    for (int k = 0; k < order; ++k) {
        const C p = std::polar(1.0, kPi * (2.0 * k + order + 1) / (2.0 * order));  // left half plane
        const C a = p * bw / 2.0;
        const C d = std::sqrt(a * a - w0sq);
        for (const C s : {a + d, a - d}) zpoles.push_back((2.0 * fs + s) / (2.0 * fs - s));
    }
    // Poles come in conjugate pairs; one biquad per pair, each with zeros at z = +1 and z = -1.
    for (const C& z : zpoles) {
        if (z.imag() <= 0.0) continue;
        sos_.push_back({1.0, 0.0, -1.0, -2.0 * z.real(), std::norm(z)});
    }
    // Normalize to unity gain at the geometric centre frequency.
    const double fc = std::atan(std::sqrt(w0sq) / (2.0 * fs)) * fs / kPi;
    const double g = magnitude(fc);
    if (g > 0.0 && !sos_.empty()) {
        const double s = std::pow(1.0 / g, 1.0 / sos_.size());
        for (auto& b : sos_) {
            b.b0 *= s;
            b.b1 *= s;
            b.b2 *= s;
        }
    }
}

void ButterworthBandpass::reset() {
    for (auto& b : sos_) b.z1 = b.z2 = 0.0;
}

void ButterworthBandpass::process(float* x, int n) {
    for (int i = 0; i < n; ++i) {
        double v = x[i];
        for (auto& b : sos_) {
            const double y = b.b0 * v + b.z1;
            b.z1 = b.b1 * v - b.a1 * y + b.z2;
            b.z2 = b.b2 * v - b.a2 * y;
            v = y;
        }
        x[i] = float(v);
    }
}

double ButterworthBandpass::magnitude(double f) const {
    const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * f / fs_);  // z^-1
    const auto z2 = z1 * z1;
    std::complex<double> h(1.0, 0.0);
    for (const auto& b : sos_) h *= (b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2);
    return std::abs(h);
}

}  // namespace fp
