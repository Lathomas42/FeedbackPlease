// Runs the detector over a WAV file as fast as possible and writes what it found.
// Use it to tune thresholds on recorded song, or to compare against LabVIEW output.
//
//   offline_detect <input.wav> [settings.ini] [out_prefix]
//
// Writes <out_prefix>_events.csv (onsets, offsets, matches, feedback) and
// <out_prefix>_envelope.csv (one row per frame).

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

#include "core/Detector.h"
#include "core/Params.h"
#include "core/WavFile.h"

using namespace fp;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <input.wav> [settings.ini] [out_prefix]\n", argv[0]);
        return 2;
    }
    const std::string wavPath = argv[1];
    AppParams p;
    if (argc >= 3) {
        std::string err;
        if (!loadParams(argv[2], p, &err)) {
            std::fprintf(stderr, "settings: %s\n", err.c_str());
            return 1;
        }
    }
    const std::string prefix = argc >= 4 ? argv[3] : wavPath.substr(0, wavPath.find_last_of('.'));

    WavData wav;
    std::string err;
    if (!readWav(wavPath, wav, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    // Keep the frame duration (ms) from the settings if the file rate differs.
    const int n = std::max(1, int(p.hw.samplesPerFrame * wav.sampleRate / p.hw.sampleRate + 0.5));
    p.hw.sampleRate = wav.sampleRate;

    Detector det;
    det.setParams(p);

    std::ofstream ev(prefix + "_events.csv"), envOut(prefix + "_envelope.csv");
    ev << "time_ms,event,ioi_ms,sd_ms,value\n";
    envOut << "time_ms,value,include_amp,subtract_amp,in_syllable,song_ratio,song_fraction,song_active,fb_state\n";

    int onsets = 0, matches = 0, blocked = 0, fires = 0, suppressed = 0, catches = 0;
    bool songWas = false;
    for (size_t i = 0; i + n <= wav.samples.size(); i += n) {
        const auto r = det.process(&wav.samples[i], n);
        envOut << r.tMs << ',' << r.value << ',' << r.includeAmp << ',' << r.subtractAmp << ','
               << r.seg.inSyllable << ',' << r.song.ratio << ',' << r.song.fraction << ',' << r.song.active << ','
               << toString(r.fbState) << '\n';
        if (r.song.active != songWas) {
            songWas = r.song.active;
            ev << r.tMs << (songWas ? ",song_on,,," : ",song_off,,,") << r.song.fraction << '\n';
        }
        if (r.matchBlocked) {
            ++blocked;
            ev << r.tMs << ",match_blocked_no_song,,," << r.value << '\n';
        }
        if (r.seg.onset) {
            ++onsets;
            ev << r.tMs << ",onset," << (r.seg.newIOI ? r.seg.ioiMs : -1) << ",," << r.value << '\n';
        }
        if (r.seg.offset) ev << r.tMs << ",offset,," << r.seg.sdMs << ',' << r.value << '\n';
        if (r.match.match) {
            ++matches;
            ev << r.tMs << ",match,,," << r.value << '\n';
        }
        if (r.fb.catchTrial) {
            ++catches;
            ev << r.tMs << ",catch,,," << r.value << '\n';
        }
        if (r.fb.fire) {
            ++fires;
            ev << r.tMs << ",feedback" << r.fb.fire << ",,," << r.value << '\n';
        }
        if (r.fb.suppressed) {
            ++suppressed;
            ev << r.tMs << ",suppressed" << r.fb.suppressed << ",,," << r.value << '\n';
        }
    }
    std::printf("%s: %.1f s @ %.0f Hz, window %d samples, frame %d samples\n", wavPath.c_str(),
                wav.samples.size() / wav.sampleRate, wav.sampleRate, det.windowSamples(), n);
    std::printf("onsets %d  matches %d (blocked by song gate %d)  feedback %d  suppressed %d  catch %d\n", onsets,
                matches, blocked, fires, suppressed, catches);
    std::printf("wrote %s_events.csv and %s_envelope.csv\n", prefix.c_str(), prefix.c_str());
    return 0;
}
