#pragma once
// Small DSP building blocks used by the song detector: a radix-2 FFT and a
// Butterworth bandpass made of biquad sections.

#include <complex>
#include <vector>

namespace fp {

// In-place iterative radix-2 FFT. x.size() must be a power of two.
void fftInPlace(std::vector<std::complex<double>>& x);
int nextPow2(int n);

// Butterworth bandpass, equivalent to LabVIEW's Butterworth Filter.vi with
// filter type = Bandpass: "order" N gives 2N poles (N biquad sections).
class ButterworthBandpass {
public:
    void design(double sampleRate, double lowHz, double highHz, int order);
    void reset();
    void process(float* x, int n);  // in place, state carried across calls
    // Magnitude response at f (for tests).
    double magnitude(double f) const;
    bool valid() const { return !sos_.empty(); }

private:
    struct Biquad {
        double b0, b1, b2, a1, a2;
        double z1 = 0.0, z2 = 0.0;  // transposed direct form II state
    };
    std::vector<Biquad> sos_;
    double fs_ = 0.0;
};

}  // namespace fp
