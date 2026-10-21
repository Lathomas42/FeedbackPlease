#include "core/Syllables.h"

#include <cmath>

namespace fp {

void SyllableSegmenter::reset() {
    inSyl_ = false;
    haveOnset_ = false;
    onsetMs_ = 0.0;
}

SegmenterOutput SyllableSegmenter::update(double value, double tMs, double startThresh, double endThresh) {
    SegmenterOutput o;
    if (!inSyl_) {
        if (value > startThresh) {
            inSyl_ = true;
            o.onset = true;
            if (haveOnset_) {
                o.newIOI = true;
                o.ioiMs = tMs - onsetMs_;
            }
            haveOnset_ = true;
            onsetMs_ = tMs;
        }
    } else if (value < endThresh) {
        inSyl_ = false;
        o.offset = true;
        o.newSD = true;
        o.sdMs = tMs - onsetMs_;
    }
    o.inSyllable = inSyl_;
    return o;
}

void SequenceMatcher::reset() {
    curIoi_ = prevIoi_ = lastSd_ = kNaN;
}

MatchResult SequenceMatcher::update(const SegmenterOutput& s, const SequenceParams& p) {
    if (s.newSD) lastSd_ = s.sdMs;
    if (s.newIOI) {
        prevIoi_ = curIoi_;
        curIoi_ = s.ioiMs;
    }

    // NaN (not yet measured) never matches.
    auto near = [](double x, double sp, double eps) { return std::fabs(x - sp) <= eps; };
    const bool ioiOk = near(curIoi_, p.ioiSetpointMs, p.ioiEpsMs);
    const bool sdOk = near(lastSd_, p.sdSetpointMs, p.sdEpsMs);
    const bool prevOk = near(prevIoi_, p.prevIoiSetpointMs, p.prevIoiEpsMs);

    MatchResult r;
    switch (p.type) {
        case TriggerType::IOI:  // at onset: the interval that just ended
            if (s.newIOI) r = {true, ioiOk};
            break;
        case TriggerType::SD:  // at offset: the syllable that just ended
            if (s.newSD) r = {true, sdOk};
            break;
        case TriggerType::IOI_SD:  // at offset: the IOI leading into this syllable, then its duration
            if (s.newSD) r = {true, ioiOk && sdOk};
            break;
        case TriggerType::SD_IOI:  // at onset: previous syllable's duration, then the IOI to this one
            if (s.newIOI) r = {true, sdOk && ioiOk};
            break;
        case TriggerType::IOI_SD_IOI:  // at onset
            if (s.newIOI) r = {true, prevOk && sdOk && ioiOk};
            break;
    }
    return r;
}

}  // namespace fp
