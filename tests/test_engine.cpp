// Runs the real-time engine headless on the simulated source for a few seconds
// and checks that frames flow, the loop keeps up, and feedback is delivered.
// Also lists NI-DAQmx devices (read-only) so you can see what the app will find.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "app/Engine.h"

#ifdef FP_HAVE_DAQMX
#include <NIDAQmx.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#endif

using namespace fp;

int main() {
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
#ifdef FP_HAVE_DAQMX
    char devs[1024] = {};
    const int32 code = DAQmxGetSysDevNames(devs, sizeof devs);
    std::printf("NI-DAQmx devices: %s\n", code < 0 ? "(query failed)" : (devs[0] ? devs : "(none)"));
    for (char* tok = std::strtok(devs, ", "); tok; tok = std::strtok(nullptr, ", ")) {
        char type[256] = {}, ai[2048] = {}, ao[1024] = {}, lines[2048] = {};
        DAQmxGetDevProductType(tok, type, sizeof type);
        DAQmxGetDevAIPhysicalChans(tok, ai, sizeof ai);
        DAQmxGetDevAOPhysicalChans(tok, ao, sizeof ao);
        DAQmxGetDevDOLines(tok, lines, sizeof lines);
        std::printf("  %s: %s\n    AI: %.80s\n    AO: %.80s\n    DO: %.80s\n", tok, type, ai, ao, lines);
    }
#endif

    AppParams p;
    p.source.kind = SourceKind::Simulated;
    p.rec.saveEvents = false;
    // One syllable code: the motif's 124 ms IOI (the 4th syllable).
    SyllableCode sylD;
    sylD.name = "D";
    sylD.code = 4;
    sylD.type = TriggerType::IOI;
    sylD.ioi = {124.0, 8.0, 116.0, 132.0};
    p.codes.list = {sylD};  // the only syllable, and the feedback target
    p.codes.target = 0;
    Engine e;
    std::string err;
    if (!e.start(p, err)) {
        std::printf("FAIL start: %s\n", err.c_str());
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(4));
    const auto s = e.stats();
    ScopeSnapshot snap;
    e.snapshot(2.0, snap);
    e.stop();

    std::printf("frames %llu in %.2f s, proc avg %.1f us max %.1f us, max backlog %.2f ms\n",
                (unsigned long long)s.frames, s.elapsedSec, s.procUsAvg, s.procUsMax, s.maxBacklogMs);
    std::printf("onsets %llu matches %llu feedback %llu; scope raw %zu env %zu\n", (unsigned long long)s.onsets,
                (unsigned long long)s.matches, (unsigned long long)s.fires1, snap.raw.size(), snap.env.size());
    int fails = 0;
    if (s.frames < 3500) { std::printf("FAIL too few frames\n"); ++fails; }
    if (s.matches < 2 || s.fires1 != s.matches) { std::printf("FAIL matches/feedback\n"); ++fails; }
    if (snap.raw.size() != 80000 || snap.env.size() != 2000) { std::printf("FAIL scope sizes\n"); ++fails; }
    if (!e.error().empty()) { std::printf("FAIL engine error: %s\n", e.error().c_str()); ++fails; }
    const uint64_t dCount = s.codeCounts.empty() ? 0 : s.codeCounts[0];
    std::printf("syllable code 'D': %llu (unmatched IOI ends %llu)\n", (unsigned long long)dCount,
                (unsigned long long)s.codeUnmatched);
    if (dCount < 2 || dCount != s.matches) { std::printf("FAIL syllable code count\n"); ++fails; }
    std::printf(fails ? "Engine test FAILED\n" : "Engine test passed\n");
    return fails ? 1 : 0;
}
