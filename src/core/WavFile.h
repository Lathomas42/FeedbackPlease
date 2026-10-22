#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace fp {

struct WavData {
    double sampleRate = 0.0;
    std::vector<float> samples;  // mono; multi-channel files keep channel 0
};

// Reads PCM 8/16/24/32-bit and IEEE float 32/64-bit WAV files.
bool readWav(const std::string& path, WavData& out, std::string* err = nullptr);

// Streams float32 samples (interleaved if multichannel) to a WAV file; the header
// is patched on close(), so a file is valid once closed.
class WavWriter {
public:
    WavWriter() = default;
    ~WavWriter();
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool open(const std::string& path, double sampleRate, int channels = 1);
    // x holds frames * channels interleaved samples.
    void write(const float* x, size_t frames);
    void close();
    bool isOpen() const { return f_ != nullptr; }

private:
    std::FILE* f_ = nullptr;
    uint64_t frames_ = 0;
    int channels_ = 1;
};

}  // namespace fp
