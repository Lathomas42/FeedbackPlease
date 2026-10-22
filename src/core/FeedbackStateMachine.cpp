#include "core/FeedbackStateMachine.h"

#include <algorithm>

namespace fp {

const char* toString(FbState s) {
    switch (s) {
        case FbState::Listening: return "Listening";
        case FbState::Triggered1: return "Triggered 1";
        case FbState::Delay1: return "Delay 1";
        case FbState::SendingTrigger1: return "Sending Trigger 1";
        case FbState::Triggered2: return "Triggered 2";
        case FbState::Delay2: return "Delay 2";
        case FbState::SendingTrigger2: return "Sending Trigger 2";
        case FbState::RefractoryPeriod: return "Refractory Period";
    }
    return "?";
}

void FeedbackStateMachine::reset() {
    st_ = FbState::Listening;
    elapsedMs_ = targetMs_ = 0.0;
    lastDetect_ = false;
}

void FeedbackStateMachine::enterRefractory() {
    st_ = FbState::RefractoryPeriod;
    elapsedMs_ = 0.0;
}

FeedbackOutput FeedbackStateMachine::update(bool detect, double value, double dtMs, const FeedbackParams& p) {
    FeedbackOutput out;
    // "Not last frame AND this frame = trigger"
    bool rising = detect && !lastDetect_;
    lastDetect_ = detect;

    // Switching feedback off cancels a trial that is waiting to deliver.
    if (!p.enabled && st_ != FbState::Listening && st_ != FbState::RefractoryPeriod) st_ = FbState::Listening;

    if (st_ == FbState::Delay1 || st_ == FbState::Delay2 || st_ == FbState::RefractoryPeriod) elapsedMs_ += dtMs;

    // LabVIEW advances one state per loop iteration, which adds a frame of latency
    // per hop. Here zero-time states fall through within the same frame.
    for (int hop = 0; hop < 8; ++hop) {
        const FbState before = st_;
        switch (st_) {
            case FbState::Listening:
                if (rising && p.enabled) {
                    rising = false;
                    out.trialStarted = true;
                    const double r = uniform();
                    const bool alt2 = p.mode2 == Feedback2Mode::Alternative && p.delay2Ms >= 0.0;
                    if (r < p.prob1) {
                        st_ = FbState::Triggered1;
                    } else if (alt2 && r < p.prob1 + p.prob2) {
                        st_ = FbState::Triggered2;
                    } else {
                        out.catchTrial = true;
                        enterRefractory();
                    }
                }
                break;
            case FbState::Triggered1:
                out.arm = 1;
                elapsedMs_ = 0.0;
                targetMs_ = std::max(0.0, p.delay1Ms) + p.extraDelayMs;
                st_ = FbState::Delay1;
                break;
            case FbState::Delay1:
                if (elapsedMs_ >= targetMs_) st_ = FbState::SendingTrigger1;
                break;
            case FbState::SendingTrigger1:
                if (value >= p.thresh1)
                    out.fire = 1;
                else
                    out.suppressed = 1;
                if (p.mode2 == Feedback2Mode::AfterFeedback1 && p.delay2Ms >= 0.0 && uniform() < p.prob2)
                    st_ = FbState::Triggered2;
                else
                    enterRefractory();
                break;
            case FbState::Triggered2:
                out.arm = 2;
                elapsedMs_ = 0.0;
                // In "follows" mode Delay 2 is measured from feedback 1, so the extra delay is not added again.
                targetMs_ = std::max(0.0, p.delay2Ms) +
                            (p.mode2 == Feedback2Mode::Alternative ? p.extraDelayMs : 0.0);
                st_ = FbState::Delay2;
                break;
            case FbState::Delay2:
                if (elapsedMs_ >= targetMs_) st_ = FbState::SendingTrigger2;
                break;
            case FbState::SendingTrigger2:
                if (value >= p.thresh2)
                    out.fire = 2;
                else
                    out.suppressed = 2;
                enterRefractory();
                break;
            case FbState::RefractoryPeriod:
                if (elapsedMs_ >= p.refractoryMs) st_ = FbState::Listening;
                break;
        }
        if (st_ == before) break;
        // At most one delivery per frame; a following feedback 2 continues next frame.
        if (st_ == FbState::RefractoryPeriod || out.fire != 0) break;
    }
    return out;
}

}  // namespace fp
