#include <chrono>
#include <thread>
#include <vector>

#include "core/SynthSong.h"
#include "core/WavFile.h"
#include "io/IOBackend.h"

namespace fp {

namespace {

// Loops a WAV file (or a synthetic song) paced to real time. Outputs are no-ops;
// the engine still logs and plots every feedback event.
class SimBackend final : public IOBackend {
public:
    bool start(const AppParams& p, std::string& err) override {
        if (!p.source.simWavPath.empty()) {
            WavData w;
            if (!readWav(p.source.simWavPath, w, &err)) return false;
            if (w.samples.empty()) {
                err = "WAV file has no samples";
                return false;
            }
            fs_ = w.sampleRate;
            data_ = std::move(w.samples);
        } else {
            fs_ = p.hw.sampleRate;
            data_ = synthesizeSong(fs_, 30.0, p.source.simNoiseVolts);
        }
        pos_ = 0;
        delivered_ = 0;
        t0_ = std::chrono::steady_clock::now();
        return true;
    }

    void stop() override {}
    double sampleRate() const override { return fs_; }

    bool read(float* dst, int n, std::string&) override {
        delivered_ += uint64_t(n);
        const auto due = t0_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                   std::chrono::duration<double>(delivered_ / fs_));
        std::this_thread::sleep_until(due);
        for (int i = 0; i < n; ++i) {
            dst[i] = data_[pos_];
            if (++pos_ == data_.size()) pos_ = 0;
        }
        return true;
    }

    size_t backlog() override {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
        const double behind = elapsed * fs_ - double(delivered_);
        return behind > 0 ? size_t(behind) : 0;
    }

    const char* name() const override { return "Simulated"; }

private:
    double fs_ = 40000.0;
    std::vector<float> data_;
    size_t pos_ = 0;
    uint64_t delivered_ = 0;
    std::chrono::steady_clock::time_point t0_;
};

}  // namespace

std::unique_ptr<IOBackend> makeSimBackend() { return std::make_unique<SimBackend>(); }

}  // namespace fp
