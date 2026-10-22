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

}  // namespace fp
