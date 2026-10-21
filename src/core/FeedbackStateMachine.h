#pragma once
// Port of the feedback state machine (TRIGGER STATE MACHINE subVI). States and
// names match the LabVIEW enum.

#include <cstdint>
#include <random>

#include "core/Params.h"

namespace fp {

enum class FbState : int {
    Listening = 0,
    Triggered1,
    Delay1,
    SendingTrigger1,
    Triggered2,
    Delay2,
    SendingTrigger2,
    RefractoryPeriod,
};
const char* toString(FbState s);

struct FeedbackOutput {
    bool trialStarted = false;  // a detection was accepted while listening
    bool catchTrial = false;    // ...but the probability draw chose no feedback
    int arm = 0;                // 1/2: preload that waveform now (we are about to delay)
    int fire = 0;               // 1/2: deliver that feedback this frame
    int suppressed = 0;         // 1/2: was due but the envelope was below "Detect Signal Thresh"
};

class FeedbackStateMachine {
public:
    explicit FeedbackStateMachine(uint32_t seed = 12345) : rng_(seed) {}

    void reset();
    // detect: true on frames where the target sequence matched.
    // value:  current envelope value, used for the send-time amplitude check.
    FeedbackOutput update(bool detect, double value, double dtMs, const FeedbackParams& p);
    FbState state() const { return st_; }

private:
    void enterRefractory();
    double uniform() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_); }

    FbState st_ = FbState::Listening;
    double elapsedMs_ = 0.0;
    double targetMs_ = 0.0;
    bool lastDetect_ = false;
    std::mt19937 rng_;
};

}  // namespace fp
