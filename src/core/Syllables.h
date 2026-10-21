#pragma once
// SyllableSegmenter: port of "Syllable and IOI Time F(x)" in DetectSyllables.vi.
// SequenceMatcher:   port of CalcDetectSignal.vi.

#include <limits>

#include "core/Params.h"

namespace fp {

struct SegmenterOutput {
    bool inSyllable = false;
    bool onset = false;   // this frame crossed above the start threshold
    bool offset = false;  // this frame dropped below the end threshold
    bool newIOI = false;  // onset with a previous onset to measure from
    bool newSD = false;   // same as offset
    double ioiMs = 0.0;   // valid when newIOI
    double sdMs = 0.0;    // valid when newSD
};

// Hysteresis segmentation: a syllable starts when the envelope rises above the
// start threshold and ends when it falls below the (lower) end threshold.
class SyllableSegmenter {
public:
    void reset();
    SegmenterOutput update(double value, double tMs, double startThresh, double endThresh);
    bool inSyllable() const { return inSyl_; }

private:
    bool inSyl_ = false;
    bool haveOnset_ = false;
    double onsetMs_ = 0.0;
};

struct MatchResult {
    bool evaluated = false;  // a comparison was made this frame
    bool match = false;
};

// Keeps the most recent IOI, the one before it and the most recent syllable
// duration, and checks them against the setpoints (|x - setpoint| <= eps).
//
// With onsets A, B, C: at C's onset curIoi = C-B, prevIoi = B-A, lastSd = duration
// of B. "IOI, SD, IOI" checks all three at C's onset, so a match means C is the
// syllable we are aiming for.
class SequenceMatcher {
public:
    void reset();
    MatchResult update(const SegmenterOutput& s, const SequenceParams& p);

    double curIoi() const { return curIoi_; }
    double prevIoi() const { return prevIoi_; }
    double lastSd() const { return lastSd_; }

private:
    static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    double curIoi_ = kNaN;
    double prevIoi_ = kNaN;
    double lastSd_ = kNaN;
};

}  // namespace fp
