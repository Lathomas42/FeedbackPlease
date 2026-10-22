#pragma once
// Multi-syllable classification: at every IOI end (syllable onset) the list of
// syllable definitions is checked in order and the first one that matches is
// reported, with its U8 code. Port of the "All Sylls Dig Code" logic.
//
// Each definition works like the feedback target (CalcDetectSignal): with onsets
// A, B, C, at C's onset IOI = C-B, SD = duration of B, previous IOI = B-A.

#include <limits>

#include "core/Params.h"
#include "core/Syllables.h"

namespace fp {

struct SylCodeResult {
    bool evaluated = false;  // an IOI ended this frame and the list was checked
    int index = -1;          // first matching entry, -1 if none
    int code = 0;            // its code (0 if none)
};

class SyllableClassifier {
public:
    void reset();
    // Call every frame with the segmenter output.
    SylCodeResult update(const SegmenterOutput& s, const SyllableCodeParams& p);

    // Whether c matches the given values (NaN = not measured yet, never matches).
    static bool matches(const SyllableCode& c, double ioi, double sd, double prevIoi);

    double ioi() const { return ioi_; }
    double sd() const { return sd_; }
    double prevIoi() const { return prevIoi_; }

private:
    static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    double ioi_ = kNaN, sd_ = kNaN, prevIoi_ = kNaN;
};

}  // namespace fp
