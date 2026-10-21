#include "core/Params.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <type_traits>

namespace fp {

namespace {

// Calls f(key, field) for every persisted field. Shared by load and save so the
// two can never drift apart.
template <class F>
void visitParams(AppParams& p, F&& f) {
    f("hw.ai_channel", p.hw.aiChannel);
    f("hw.terminal_config", p.hw.terminalConfig);
    f("hw.ai_min", p.hw.aiMin);
    f("hw.ai_max", p.hw.aiMax);
    f("hw.sample_rate", p.hw.sampleRate);
    f("hw.samples_per_frame", p.hw.samplesPerFrame);
    f("hw.input_buffer_sec", p.hw.inputBufferSec);
    f("hw.ao_channel", p.hw.aoChannel);
    f("hw.ao_min", p.hw.aoMin);
    f("hw.ao_max", p.hw.aoMax);
    f("hw.ao_scale", p.hw.aoScale);
    f("hw.do_feedback_line", p.hw.doFeedbackLine);
    f("hw.do_syllable_line", p.hw.doSyllableLine);
    f("hw.do_match_line", p.hw.doMatchLine);

    f("source.kind", p.source.kind);
    f("source.sim_wav_path", p.source.simWavPath);
    f("source.sim_noise_volts", p.source.simNoiseVolts);

    f("spectrum.window_ms", p.spectrum.windowMs);
    for (int i = 0; i < kBandsPerGroup; ++i) {
        const std::string inc = "spectrum.include" + std::to_string(i);
        f(inc + ".gain", p.spectrum.include[i].gain);
        f(inc + ".low_hz", p.spectrum.include[i].lowHz);
        f(inc + ".high_hz", p.spectrum.include[i].highHz);
        const std::string sub = "spectrum.subtract" + std::to_string(i);
        f(sub + ".gain", p.spectrum.subtract[i].gain);
        f(sub + ".low_hz", p.spectrum.subtract[i].lowHz);
        f(sub + ".high_hz", p.spectrum.subtract[i].highHz);
    }
    f("spectrum.combination", p.spectrum.combination);
    f("spectrum.syl_start_thresh", p.spectrum.sylStartThresh);
    f("spectrum.syl_end_thresh", p.spectrum.sylEndThresh);

    f("proc.mean_subtract", p.proc.meanSubtract);
    f("proc.low_cutoff", p.proc.lowCutoff);
    f("proc.low_cutoff_thresh", p.proc.lowCutoffThresh);
    f("proc.software_gain", p.proc.softwareGain);
    f("proc.gain", p.proc.gain);
    f("proc.display_db", p.proc.displayDb);

    f("seq.trigger_type", p.seq.type);
    f("seq.ioi_setpoint_ms", p.seq.ioiSetpointMs);
    f("seq.ioi_eps_ms", p.seq.ioiEpsMs);
    f("seq.sd_setpoint_ms", p.seq.sdSetpointMs);
    f("seq.sd_eps_ms", p.seq.sdEpsMs);
    f("seq.prev_ioi_setpoint_ms", p.seq.prevIoiSetpointMs);
    f("seq.prev_ioi_eps_ms", p.seq.prevIoiEpsMs);

    f("fb.enabled", p.fb.enabled);
    f("fb.prob1", p.fb.prob1);
    f("fb.delay1_ms", p.fb.delay1Ms);
    f("fb.thresh1", p.fb.thresh1);
    f("fb.wave1", p.fb.wave1);
    f("fb.prob2", p.fb.prob2);
    f("fb.delay2_ms", p.fb.delay2Ms);
    f("fb.thresh2", p.fb.thresh2);
    f("fb.wave2", p.fb.wave2);
    f("fb.mode2", p.fb.mode2);
    f("fb.refractory_ms", p.fb.refractoryMs);
    f("fb.extra_delay_ms", p.fb.extraDelayMs);
    f("fb.ttl_pulse_ms", p.fb.ttlPulseMs);

    f("song.enabled", p.song.enabled);
    f("song.gate_feedback", p.song.gateFeedback);
    f("song.mean_subtract", p.song.meanSubtract);
    f("song.gain", p.song.gain);
    f("song.offset", p.song.offset);
    f("song.butterworth", p.song.butterworth);
    f("song.filter_order", p.song.filterOrder);
    f("song.filter_low_hz", p.song.filterLowHz);
    f("song.filter_high_hz", p.song.filterHighHz);
    f("song.window_length", p.song.windowLength);
    f("song.frequency_bins", p.song.frequencyBins);
    f("song.hop_ms", p.song.hopMs);
    f("song.non_bird_cutoff_hz", p.song.nonBirdCutoffHz);
    f("song.bird_low_hz", p.song.birdLowHz);
    f("song.bird_high_hz", p.song.birdHighHz);
    f("song.ratio_mode", p.song.ratioMode);
    f("song.ratio_thresh", p.song.ratioThresh);
    f("song.min_bird_rms", p.song.minBirdRms);
    f("song.song_duration_sec", p.song.songDurationSec);
    f("song.duration_thresh", p.song.durationThresh);
    f("song.hold_sec", p.song.holdSec);
    f("hw.do_song_line", p.hw.doSongLine);

    f("display.window_sec", p.display.windowSec);
    f("display.raw_auto_y", p.display.rawAutoY);
    f("display.raw_y_range", p.display.rawYRange);
    f("display.env_auto_y", p.display.envAutoY);

    f("rec.output_dir", p.rec.outputDir);
    f("rec.save_events", p.rec.saveEvents);
    f("rec.save_raw", p.rec.saveRaw);
    f("rec.save_envelope", p.rec.saveEnvelope);
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

struct Saver {
    std::ostream& os;
    void operator()(const std::string& k, std::string& v) { os << k << " = " << v << "\n"; }
    void operator()(const std::string& k, double& v) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.10g", v);
        os << k << " = " << buf << "\n";
    }
    void operator()(const std::string& k, int& v) { os << k << " = " << v << "\n"; }
    void operator()(const std::string& k, bool& v) { os << k << " = " << (v ? 1 : 0) << "\n"; }
    template <class E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
    void operator()(const std::string& k, E& v) {
        os << k << " = " << static_cast<int>(v) << "  # " << toString(v) << "\n";
    }
};

struct Loader {
    const std::map<std::string, std::string>& kv;
    std::string& err;
    const std::string* find(const std::string& k) const {
        auto it = kv.find(k);
        return it == kv.end() ? nullptr : &it->second;
    }
    void fail(const std::string& k) { err += "bad value for " + k + "; "; }
    void operator()(const std::string& k, std::string& v) {
        if (auto s = find(k)) v = *s;
    }
    void operator()(const std::string& k, double& v) {
        if (auto s = find(k)) {
            try { v = std::stod(*s); } catch (...) { fail(k); }
        }
    }
    void operator()(const std::string& k, int& v) {
        if (auto s = find(k)) {
            try { v = std::stoi(*s); } catch (...) { fail(k); }
        }
    }
    void operator()(const std::string& k, bool& v) {
        if (auto s = find(k)) v = (*s == "1" || *s == "true" || *s == "TRUE");
    }
    template <class E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
    void operator()(const std::string& k, E& v) {
        if (auto s = find(k)) {
            try { v = static_cast<E>(std::stoi(*s)); } catch (...) { fail(k); }
        }
    }
};

}  // namespace

bool loadParams(const std::string& path, AppParams& p, std::string* err) {
    std::ifstream in(path);
    if (!in) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = line.substr(eq + 1);
        // Strip trailing "# comment" for non-path values (paths may contain '#').
        if (key.find("path") == std::string::npos && key.find("wave") == std::string::npos &&
            key.find("dir") == std::string::npos) {
            const auto hash = val.find('#');
            if (hash != std::string::npos) val = val.substr(0, hash);
        }
        kv[key] = trim(val);
    }
    std::string localErr;
    visitParams(p, Loader{kv, localErr});
    if (err) *err = localErr;
    return localErr.empty();
}

bool saveParams(const std::string& path, const AppParams& p, std::string* err) {
    std::ofstream out(path);
    if (!out) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    out << "# FeedbackPlease settings\n";
    AppParams copy = p;
    visitParams(copy, Saver{out});
    return true;
}

const char* toString(BandCombination c) {
    switch (c) {
        case BandCombination::Difference: return "Difference";
        case BandCombination::Sum: return "Sum";
        case BandCombination::Ratio: return "Ratio";
        case BandCombination::IncludeOnly: return "Include only";
    }
    return "?";
}

const char* toString(TriggerType t) {
    switch (t) {
        case TriggerType::IOI: return "IOI";
        case TriggerType::SD: return "SD";
        case TriggerType::IOI_SD: return "IOI, SD";
        case TriggerType::SD_IOI: return "SD, IOI";
        case TriggerType::IOI_SD_IOI: return "IOI, SD, IOI";
    }
    return "?";
}

const char* toString(Feedback2Mode m) {
    switch (m) {
        case Feedback2Mode::Alternative: return "Alternative to feedback 1";
        case Feedback2Mode::AfterFeedback1: return "Follows feedback 1";
    }
    return "?";
}

const char* toString(SourceKind k) {
    switch (k) {
        case SourceKind::Daq: return "NI-DAQmx";
        case SourceKind::Simulated: return "Simulated (WAV / synthetic)";
    }
    return "?";
}

const char* toString(SongRatioMode m) {
    switch (m) {
        case SongRatioMode::BirdOverLow: return "Bird / Non-bird";
        case SongRatioMode::BirdOverTotal: return "Bird / (Bird + Non-bird)";
    }
    return "?";
}

const char* toString(TerminalConfig t) {
    switch (t) {
        case TerminalConfig::Default: return "Default";
        case TerminalConfig::RSE: return "RSE";
        case TerminalConfig::NRSE: return "NRSE";
        case TerminalConfig::Differential: return "Differential";
        case TerminalConfig::PseudoDiff: return "Pseudodifferential";
    }
    return "?";
}

}  // namespace fp
