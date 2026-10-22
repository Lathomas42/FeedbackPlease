#include "core/SyllableCodes.h"

#include <algorithm>

namespace fp {

void SyllableClassifier::reset() { ioi_ = sd_ = prevIoi_ = kNaN; }

bool SyllableClassifier::matches(const SyllableCode& c, double ioi, double sd, double prevIoi) {
    if (!c.enabled || !isOnsetEvaluated(c.type)) return false;
    if (usesIoi(c.type) && !c.ioi.contains(ioi, c.mode)) return false;
    if (usesSd(c.type) && !c.sd.contains(sd, c.mode)) return false;
    if (usesPrevIoi(c.type) && !c.prevIoi.contains(prevIoi, c.mode)) return false;
    return true;
}

SylCodeResult SyllableClassifier::update(const SegmenterOutput& s, const SyllableCodeParams& p) {
    // An offset and an onset never happen in the same frame (hysteresis), so the
    // previous syllable's SD is in place before its successor's onset.
    if (s.newSD) sd_ = s.sdMs;
    SylCodeResult r;
    if (!s.newIOI) return r;
    prevIoi_ = ioi_;
    ioi_ = s.ioiMs;
    // Always classify: the feedback target depends on it even when the port is off.
    r.evaluated = true;
    for (int i = 0; i < int(p.list.size()); ++i)
        if (matches(p.list[i], ioi_, sd_, prevIoi_)) {
            r.index = i;
            r.code = std::clamp(p.list[i].code, 0, 255);
            break;
        }
    return r;
}

}  // namespace fp
