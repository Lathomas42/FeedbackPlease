// Minimal self-checking tests for the detection core (no framework needed).
// Run: build/Release/fp_tests.exe   (or `ctest -C Release`)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/BandEnvelope.h"
#include "core/Detector.h"
#include "core/FeedbackStateMachine.h"
#include "core/Filters.h"
#include "core/Offline.h"
#include "core/Params.h"
#include "core/SongDetector.h"
#include "core/SynthSong.h"
#include "core/Syllables.h"

using namespace fp;

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static void testBandEnvelopeSine() {
    const double fs = 40000.0;
    const int n = 600;
    SpectrumParams sp;
    sp.include = {{{1.0, 1000, 7000}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
    sp.subtract = {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
    sp.combination = BandCombination::IncludeOnly;
    ProcessingParams pp;
    pp.softwareGain = false;
    pp.meanSubtract = true;

    BandEnvelope env;
    env.configure(fs, n, sp, pp);
    std::vector<float> x(n);

    // A 0.5 V sine in band should read as its RMS, 0.354 V.
    for (int i = 0; i < n; ++i) x[i] = float(0.5 * std::sin(2 * 3.14159265358979 * 3000.0 * i / fs) + 0.3);  // + DC
    const double inBand = env.compute(x.data()).value;
    std::printf("  3 kHz sine RMS in band: %.4f (expect ~0.354)\n", inBand);
    CHECK(std::fabs(inBand - 0.3536) < 0.02);

    // Out of band (300 Hz) should be mostly rejected.
    for (int i = 0; i < n; ++i) x[i] = float(0.5 * std::sin(2 * 3.14159265358979 * 300.0 * i / fs));
    const double outBand = env.compute(x.data()).value;
    std::printf("  300 Hz sine in 1-7 kHz band: %.4f (expect ~0)\n", outBand);
    CHECK(outBand < 0.01);
}

static void testSegmenterAndMatcher() {
    SyllableSegmenter seg;
    SequenceMatcher m;
    SequenceParams sp;  // IOI,SD,IOI with 124 / 95 / 95 +-10
    // Onsets at 0, 95, 219 ms. Syllable starting at 95 lasts 95 ms.
    struct Ev { double t; double v; };
    std::vector<Ev> evs = {{0, 1}, {40, 0}, {95, 1}, {190, 0}, {219, 1}};
    int matches = 0, evaluations = 0;
    for (const auto& e : evs) {
        auto o = seg.update(e.v, e.t, 0.5, 0.2);
        auto r = m.update(o, sp);
        evaluations += r.evaluated;
        matches += r.match;
    }
    CHECK(evaluations == 2);  // at the 2nd and 3rd onsets
    CHECK(matches == 1);
    CHECK(std::fabs(m.curIoi() - 124) < 1e-9);
    CHECK(std::fabs(m.prevIoi() - 95) < 1e-9);
    CHECK(std::fabs(m.lastSd() - 95) < 1e-9);

    // Hysteresis: dipping between the thresholds does not end a syllable.
    SyllableSegmenter s2;
    CHECK(s2.update(1.0, 0, 0.5, 0.2).onset);
    CHECK(!s2.update(0.3, 1, 0.5, 0.2).offset);
    CHECK(s2.update(0.1, 2, 0.5, 0.2).offset);
}

static void testStateMachine() {
    FeedbackParams p;  // prob1 = 1, delay1 = 20 ms, refractory = 100 ms
    FeedbackStateMachine fsm(7);
    int fireFrame = -1, armFrame = -1;
    for (int f = 0; f < 300; ++f) {
        const bool detect = (f == 10) || (f == 50);  // second detect falls inside the trial and is ignored
        auto o = fsm.update(detect, 1.0, 1.0, p);
        if (o.arm == 1) armFrame = f;
        if (o.fire == 1) {
            CHECK(fireFrame < 0);
            fireFrame = f;
        }
    }
    CHECK(armFrame == 10);
    CHECK(fireFrame == 30);
    CHECK(fsm.state() == FbState::Listening);

    // Amplitude check at send time.
    FeedbackStateMachine fsm2(7);
    p.delay1Ms = 0;
    p.thresh1 = 0.5;
    auto o = fsm2.update(true, 0.1, 1.0, p);
    CHECK(o.fire == 0 && o.suppressed == 1);

    // Probability 0 gives a catch trial.
    FeedbackStateMachine fsm3(7);
    p.prob1 = 0;
    o = fsm3.update(true, 1.0, 1.0, p);
    CHECK(o.trialStarted && o.catchTrial && o.fire == 0);
}

static void testEndToEndSynthetic() {
    AppParams p;  // defaults from the LabVIEW panel
    const double fs = p.hw.sampleRate;
    const auto song = synthesizeSong(fs, 10.0, p.source.simNoiseVolts);
    Detector det(3);
    det.setParams(p);
    const int n = p.hw.samplesPerFrame;
    int onsets = 0, matches = 0, fires = 0;
    double firstFireMs = -1;
    for (size_t i = 0; i + n <= song.size(); i += n) {
        auto r = det.process(&song[i], n);
        if (r.seg.onset) ++onsets;
        if (r.seg.newSD && onsets <= 5) std::printf("  SD %.0f ms\n", r.seg.sdMs);
        if (r.seg.newIOI && onsets <= 5) std::printf("  IOI %.0f ms\n", r.seg.ioiMs);
        matches += r.match.match;
        if (r.fb.fire) {
            ++fires;
            if (firstFireMs < 0) firstFireMs = r.tMs;
        }
    }
    std::printf("  synthetic 10 s: %d onsets, %d matches, %d feedbacks, first at %.0f ms\n", onsets, matches, fires,
                firstFireMs);
    const int motifs = 10000 / (90 + 95 + 124 + 150 + 130 + 600);  // ~8 complete motifs
    CHECK(onsets >= 5 * motifs);
    CHECK(matches >= motifs);
    CHECK(matches <= motifs + 1);  // exactly one target per motif
    CHECK(fires == matches);
}

static void testButterworth() {
    ButterworthBandpass bp;
    bp.design(40000.0, 3000.0, 10000.0, 6);
    const double pass = bp.magnitude(5477.0), edgeLo = bp.magnitude(3000.0), edgeHi = bp.magnitude(10000.0);
    const double stop = bp.magnitude(1000.0);
    std::printf("  |H| centre %.3f, 3 kHz %.3f, 10 kHz %.3f, 1 kHz %.2e\n", pass, edgeLo, edgeHi, stop);
    CHECK(std::fabs(pass - 1.0) < 0.01);
    CHECK(std::fabs(edgeLo - 0.7071) < 0.02 && std::fabs(edgeHi - 0.7071) < 0.02);  // -3 dB edges
    CHECK(stop < 1e-3);

    // Streaming a tone through it: in-band passes, out-of-band is removed.
    auto rms = [&](double f) {
        ButterworthBandpass b = bp;
        b.reset();
        std::vector<float> x(8000);
        for (size_t i = 0; i < x.size(); ++i) x[i] = float(std::sin(2 * 3.14159265358979 * f * i / 40000.0));
        b.process(x.data(), int(x.size()));
        double s = 0;
        for (size_t i = 4000; i < x.size(); ++i) s += x[i] * x[i];
        return std::sqrt(s / 4000);
    };
    CHECK(std::fabs(rms(6000) - 0.7071) < 0.02);
    CHECK(rms(500) < 1e-3);
}

static void testSongDetector() {
    const double fs = 40000.0;
    AppParams p;
    auto run = [&](const std::vector<float>& x, double* maxFrac) {
        SongDetector sd;
        sd.configure(fs, p.song);
        int activeFrames = 0;
        *maxFrac = 0;
        for (size_t i = 0; i + 40 <= x.size(); i += 40) {
            const auto& s = sd.process(&x[i], 40);
            activeFrames += s.active;
            *maxFrac = std::max(*maxFrac, s.fraction);
        }
        return activeFrames;
    };
    double frac;

    // Song: detected for a good part of the time.
    const auto song = synthesizeSong(fs, 5.0, 0.0005);
    const int songFrames = run(song, &frac);
    std::printf("  song: active %d of %d ms, max fraction %.2f\n", songFrames, int(song.size() / 40), frac);
    CHECK(songFrames > 1500 && frac > 0.5);

    // Loud low-frequency cage noise alone: never song.
    const auto cage = synthesizeSong(fs, 5.0, 0.0005, 1, {}, 600.0, 0.2, 0.05);
    CHECK(run(cage, &frac) == 0);
    std::printf("  cage noise only: max fraction %.2f\n", frac);

    // An isolated 40 ms call every 1.5 s: too short to fill 25% of the 0.5 s kernel.
    auto calls = synthesizeSong(fs, 5.0, 0.0005, 1, {}, 600.0, 0.2, 0.003);
    for (double t = 300; t < 4800; t += 1500) addSyllable(calls, fs, t, 40, 700);
    CHECK(run(calls, &frac) == 0);
    std::printf("  isolated calls: max fraction %.2f\n", frac);
}

static void testSongGate() {
    // Same song, but every 3rd motif is replaced by the target triplet alone with
    // long gaps around it... simpler: gate on vs. a song detector that can never fire.
    AppParams p;
    p.song.ratioThresh = 1e9;  // nothing counts as song
    const auto song = synthesizeSong(p.hw.sampleRate, 5.0, p.source.simNoiseVolts);
    Detector det(3);
    det.setParams(p);
    int matches = 0, blocked = 0, fires = 0;
    for (size_t i = 0; i + 40 <= song.size(); i += 40) {
        auto r = det.process(&song[i], 40);
        matches += r.match.match;
        blocked += r.matchBlocked;
        fires += r.fb.fire != 0;
    }
    CHECK(matches > 0 && blocked == matches && fires == 0);
    p.song.gateFeedback = false;  // gate off -> feedback again
    Detector det2(3);
    det2.setParams(p);
    fires = 0;
    for (size_t i = 0; i + 40 <= song.size(); i += 40) fires += det2.process(&song[i], 40).fb.fire != 0;
    CHECK(fires == matches);
}

static void testOfflineMatchesRealtime() {
    AppParams p;
    p.fb.prob1 = 0.7;  // exercise the random draw too
    const auto song = synthesizeSong(p.hw.sampleRate, 12.0, p.source.simNoiseVolts);
    const AppParams pr = paramsForRate(p, p.hw.sampleRate);

    Detector det(99);
    det.setParams(pr);
    const int n = pr.hw.samplesPerFrame;
    std::vector<float> rtValue;
    std::vector<double> rtOnsets, rtFires, rtMatches;
    for (size_t i = 0; i + n <= song.size(); i += n) {
        const auto r = det.process(&song[i], n);
        rtValue.push_back(float(r.value));
        if (r.seg.onset) rtOnsets.push_back(r.tMs);
        if (r.fb.fire) rtFires.push_back(r.tMs);
        if (r.match.match && !r.matchBlocked) rtMatches.push_back(r.tMs);
    }

    EnvelopeTrack env;
    SongTrack st;
    DecisionTrack dt;
    CHECK(computeEnvelopeTrack(song, pr, env));
    computeSongTrack(song, pr, st);
    runDecisions(env, st, pr, dt, 99);

    CHECK(env.value == rtValue);  // bit-identical
    std::vector<double> offOnsets;
    for (const auto& s : dt.syllables) offOnsets.push_back(s.onsetMs);
    std::vector<double> offFires = dt.fire1Ms;
    CHECK(offOnsets == rtOnsets);
    CHECK(dt.matchMs == rtMatches);
    CHECK(offFires == rtFires);
    std::printf("  %zu frames, %zu syllables, %zu matches, %zu feedback: identical\n", env.value.size(),
                dt.syllables.size(), dt.matchMs.size(), offFires.size());

    // A syllable that matched should carry the setpoint values that made it match.
    for (const auto& s : dt.syllables)
        if (s.matched) {
            CHECK(std::fabs(s.ioiMs - p.seq.ioiSetpointMs) <= p.seq.ioiEpsMs);
            CHECK(std::fabs(s.prevSdMs - p.seq.sdSetpointMs) <= p.seq.sdEpsMs);
            CHECK(std::fabs(s.prevIoiMs - p.seq.prevIoiSetpointMs) <= p.seq.prevIoiEpsMs);
        }
}

static void testParamsRoundTrip() {
    AppParams a;
    a.hw.aiChannel = "PXI1Slot2/ai3";
    a.spectrum.include[2] = {2.5, 300, 900};
    a.seq.type = TriggerType::SD_IOI;
    a.fb.wave1 = "C:\\stim\\noise #1.wav";
    CHECK(saveParams("fp_test_params.ini", a));
    AppParams b;
    CHECK(loadParams("fp_test_params.ini", b));
    CHECK(b.hw.aiChannel == a.hw.aiChannel);
    CHECK(b.spectrum.include[2].gain == 2.5 && b.spectrum.include[2].highHz == 900);
    CHECK(b.seq.type == TriggerType::SD_IOI);
    CHECK(b.fb.wave1 == a.fb.wave1);
    std::remove("fp_test_params.ini");
}

int main() {
    std::printf("BandEnvelope\n");
    testBandEnvelopeSine();
    std::printf("Segmenter + matcher\n");
    testSegmenterAndMatcher();
    std::printf("Feedback state machine\n");
    testStateMachine();
    std::printf("End to end (synthetic song)\n");
    testEndToEndSynthetic();
    std::printf("Butterworth bandpass\n");
    testButterworth();
    std::printf("Song detector\n");
    testSongDetector();
    std::printf("Song gate\n");
    testSongGate();
    std::printf("Offline analysis == real time\n");
    testOfflineMatchesRealtime();
    std::printf("Params round trip\n");
    testParamsRoundTrip();
    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All tests passed\n");
    return 0;
}
