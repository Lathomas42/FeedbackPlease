#pragma once
// SyllableSegmenter: port of "Syllable and IOI Time F(x)" in DetectSyllables.vi.
// (CalcDetectSignal.vi's matching now lives in SyllableClassifier.)

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

// Whether the target syllable was recognized this frame (see SyllableClassifier).
struct MatchResult {
    bool evaluated = false;  // an IOI ended and the syllable list was checked
    bool match = false;      // the first matching syllable is the feedback target
};

}  // namespace fp
