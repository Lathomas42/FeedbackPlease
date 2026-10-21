#include "app/Engine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fp {

namespace {
constexpr double kScopeSeconds = 10.0;  // how much history the GUI can show
constexpr size_t kMaxEvents = 1000;

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d_%H%M%S", &tm);
    return buf;
}

std::string fmt(const char* f, double a, double b = 0, double c = 0) {
    char buf[160];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}
}  // namespace

// ---------------------------------------------------------------- AsyncRecorder

bool AsyncRecorder::open(const std::string& rawPath, double rawRate, const std::string& envPath, double envRate) {
    close();
    if (!rawPath.empty() && !raw_.open(rawPath, rawRate)) return false;
    if (!envPath.empty() && !env_.open(envPath, envRate)) return false;
    if (!raw_.isOpen() && !env_.isOpen()) return true;
    stop_ = false;
    th_ = std::thread(&AsyncRecorder::run, this);
    return true;
}

void AsyncRecorder::pushRaw(const float* x, size_t n) {
    if (!raw_.isOpen()) return;
    std::lock_guard<std::mutex> lk(mx_);
    pendRaw_.insert(pendRaw_.end(), x, x + n);
}

void AsyncRecorder::pushEnv(float v) {
    if (!env_.isOpen()) return;
    std::lock_guard<std::mutex> lk(mx_);
    pendEnv_.push_back(v);
}

void AsyncRecorder::run() {
    std::vector<float> r, e;
    for (;;) {
        bool done;
        {
            std::unique_lock<std::mutex> lk(mx_);
            cv_.wait_for(lk, std::chrono::milliseconds(100), [&] { return stop_; });
            r.swap(pendRaw_);
            e.swap(pendEnv_);
            done = stop_;
        }
        raw_.write(r.data(), r.size());
        env_.write(e.data(), e.size());
        r.clear();
        e.clear();
        if (done) break;
    }
}

void AsyncRecorder::close() {
    if (th_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(mx_);
            stop_ = true;
        }
        cv_.notify_all();
        th_.join();
    }
    raw_.close();
    env_.close();
    pendRaw_.clear();
    pendEnv_.clear();
}

// ---------------------------------------------------------------- Engine

bool Engine::start(const AppParams& params, std::string& err) {
    stop();
    {
        std::lock_guard<std::mutex> lk(mx_);
        error_.clear();
    }

    io_ = params.source.kind == SourceKind::Daq ? makeDaqBackend() : makeSimBackend();
    if (!io_->start(params, err)) {
        io_.reset();
        return false;
    }

    AppParams p = params;
    const double fs = io_->sampleRate();
    // Keep the frame duration when a simulated WAV has a different rate than the settings.
    const int frameN = std::max(1, int(std::lround(p.hw.samplesPerFrame * fs / p.hw.sampleRate)));
    p.hw.sampleRate = fs;
    p.hw.samplesPerFrame = frameN;
    startParams_ = p;
    {
        std::lock_guard<std::mutex> lk(paramMx_);
        pending_ = p;
    }

    if (!openSession(p, err)) {
        io_->stop();
        io_.reset();
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(mx_);
        const std::string prefix = stats_.sessionPrefix;
        stats_ = EngineStats{};
        stats_.sessionPrefix = prefix;
        stats_.running = true;
        stats_.backend = io_->name();
        stats_.sampleRate = fs;
        stats_.frameSamples = frameN;
        raw_.reset(size_t(fs * kScopeSeconds));
        const double frameRate = fs / frameN;
        for (auto* r : {&env_, &syl_, &songFrac_, &songRatio_, &songOn_}) r->reset(size_t(frameRate * kScopeSeconds));
        matchT_.clear();
        fireT_.clear();
        suppressT_.clear();
        blockedT_.clear();
        events_.clear();
    }

    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread(&Engine::run, this);
    return true;
}

void Engine::stop() {
    stopRequested_ = true;
    if (thread_.joinable()) thread_.join();
    if (io_) {
        io_->stop();
        io_.reset();
    }
    running_ = false;
    recorder_.close();
    if (eventsCsv_.is_open()) eventsCsv_.close();
    std::lock_guard<std::mutex> lk(mx_);
    stats_.running = false;
}

bool Engine::openSession(const AppParams& p, std::string& err) {
    std::string prefix;
    const auto& rec = p.rec;
    if (rec.saveEvents || rec.saveRaw || rec.saveEnvelope) {
        std::error_code ec;
        std::filesystem::create_directories(rec.outputDir, ec);
        if (ec) {
            err = "cannot create " + rec.outputDir + ": " + ec.message();
            return false;
        }
        prefix = (std::filesystem::path(rec.outputDir) / ("session_" + timestamp())).string();
        saveParams(prefix + "_settings.ini", p);
        if (rec.saveEvents) {
            eventsCsv_.open(prefix + "_events.csv");
            if (!eventsCsv_) {
                err = "cannot write " + prefix + "_events.csv";
                return false;
            }
            eventsCsv_ << "sample,time_ms,event,value,detail\n";
        }
        const double fs = p.hw.sampleRate;
        if (!recorder_.open(rec.saveRaw ? prefix + "_raw.wav" : "", fs,
                            rec.saveEnvelope ? prefix + "_envelope.wav" : "", fs / p.hw.samplesPerFrame)) {
            err = "cannot open recording files at " + prefix;
            return false;
        }
    }
    std::lock_guard<std::mutex> lk(mx_);
    stats_.sessionPrefix = prefix;
    return true;
}

void Engine::updateParams(const AppParams& p) {
    std::lock_guard<std::mutex> lk(paramMx_);
    pending_ = p;
    ++paramVersion_;
}

void Engine::logEvent(double tMs, const char* type, const std::string& detail, double value) {
    if (eventsCsv_.is_open()) {
        const uint64_t sample = uint64_t(std::llround(tMs * startParams_.hw.sampleRate / 1000.0));
        eventsCsv_ << sample << ',' << tMs << ',' << type << ',' << value << ',' << detail << '\n';
    }
    std::lock_guard<std::mutex> lk(mx_);
    events_.push_back({tMs, type, detail});
    if (events_.size() > kMaxEvents) events_.pop_front();
}

void Engine::run() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
    using clock = std::chrono::steady_clock;

    const double fs = startParams_.hw.sampleRate;
    const int n = startParams_.hw.samplesPerFrame;
    const double frameMs = n * 1000.0 / fs;

    Detector det(uint32_t(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    AppParams p = startParams_;
    det.setParams(p);
    uint64_t seenVersion = paramVersion_.load();

    std::vector<float> frame(size_t(n), 0.0f);
    DigitalOutputs dout;
    double fbPulseLeftMs = 0.0, matchPulseLeftMs = 0.0;
    double procAvg = 0.0, procMax = 0.0, maxBacklog = 0.0;
    uint64_t frames = 0;
    uint64_t onsets = 0, matches = 0, blocked = 0, trials = 0, fires1 = 0, fires2 = 0, suppressed = 0, catches = 0;
    uint64_t songBouts = 0;
    bool songWasActive = false;
    double lastIoi = 0.0, lastSd = 0.0;
    std::string err;

    while (!stopRequested_) {
        if (!io_->read(frame.data(), n, err)) {
            std::lock_guard<std::mutex> lk(mx_);
            error_ = err;
            break;
        }
        const auto t0 = clock::now();

        if (paramVersion_.load() != seenVersion) {
            std::lock_guard<std::mutex> lk(paramMx_);
            seenVersion = paramVersion_.load();
            AppParams np = pending_;
            np.hw = startParams_.hw;  // hardware is fixed for the run
            p = np;
            det.setParams(p);
        }
        if (resetCounters_.exchange(false)) {
            onsets = matches = blocked = trials = fires1 = fires2 = suppressed = catches = songBouts = 0;
            procMax = maxBacklog = 0.0;
        }

        FrameResult r = det.process(frame.data(), n);

        // A manual test fire bypasses detection and the state machine.
        const int manual = manualFire_.exchange(0);
        if (manual) {
            r.fb.arm = 0;
            r.fb.fire = manual;
        }

        // ---- outputs first, bookkeeping after
        if (r.fb.arm) io_->armWave(r.fb.arm);
        if (r.fb.fire) {
            io_->fireWave(r.fb.fire);
            fbPulseLeftMs = std::max(p.fb.ttlPulseMs, frameMs);
        }
        if (r.match.match) matchPulseLeftMs = std::max(p.fb.ttlPulseMs, frameMs);
        dout.feedback = fbPulseLeftMs > 0.0;
        dout.match = matchPulseLeftMs > 0.0;
        dout.syllable = r.seg.inSyllable;
        dout.song = r.song.active;
        io_->writeDigital(dout);
        fbPulseLeftMs -= frameMs;
        matchPulseLeftMs -= frameMs;
        io_->service();

        const double procUs = std::chrono::duration<double, std::micro>(clock::now() - t0).count();
        procAvg = frames == 0 ? procUs : 0.99 * procAvg + 0.01 * procUs;
        if (frames > 10) procMax = std::max(procMax, procUs);  // skip warm-up
        ++frames;
        const size_t backlog = io_->backlog();
        const double backlogMs = backlog * 1000.0 / fs;
        maxBacklog = std::max(maxBacklog, backlogMs);

        // ---- events
        const double t = r.tMs;
        if (r.seg.onset) {
            ++onsets;
            if (r.seg.newIOI) lastIoi = r.seg.ioiMs;
            logEvent(t, "onset", r.seg.newIOI ? fmt("IOI %.0f ms", r.seg.ioiMs) : "first", r.value);
        }
        if (r.seg.offset) {
            lastSd = r.seg.sdMs;
            logEvent(t, "offset", fmt("SD %.0f ms", r.seg.sdMs), r.value);
        }
        if (r.song.active != songWasActive) {
            songWasActive = r.song.active;
            if (r.song.active) ++songBouts;
            logEvent(t, r.song.active ? "song on" : "song off", fmt("fraction %.2f", r.song.fraction), r.song.ratio);
        }
        if (r.match.match) {
            ++matches;
            if (r.matchBlocked) ++blocked;
            logEvent(t, "match", std::string(toString(p.seq.type)) + (r.matchBlocked ? "  (blocked: no song)" : ""),
                     r.value);
        }
        if (r.fb.trialStarted) ++trials;
        if (r.fb.catchTrial) {
            ++catches;
            logEvent(t, "catch", "probability draw: no feedback", r.value);
        }
        if (r.fb.fire) {
            (r.fb.fire == 1 ? fires1 : fires2)++;
            logEvent(t, r.fb.fire == 1 ? "feedback1" : "feedback2", manual ? "manual" : "", r.value);
        }
        if (r.fb.suppressed) {
            ++suppressed;
            logEvent(t, "suppressed", fmt("feedback %.0f below detect-signal threshold", r.fb.suppressed), r.value);
        }
        const std::string outErr = io_->takeOutputError();
        if (!outErr.empty()) logEvent(t, "error", outErr, 0.0);

        recorder_.pushRaw(frame.data(), frame.size());
        recorder_.pushEnv(float(r.value));

        // ---- GUI-visible state
        {
            std::lock_guard<std::mutex> lk(mx_);
            for (float v : frame) raw_.push(v);
            env_.push(float(r.displayValue));
            syl_.push(r.seg.inSyllable ? 1.0f : 0.0f);
            songFrac_.push(float(r.song.fraction));
            songRatio_.push(float(r.song.ratio));
            songOn_.push(r.song.active ? 1.0f : 0.0f);
            const double ts = t / 1000.0;
            if (r.match.match) (r.matchBlocked ? blockedT_ : matchT_).push_back(ts);
            if (r.fb.fire) fireT_.push_back(ts);
            if (r.fb.suppressed) suppressT_.push_back(ts);
            for (auto* q : {&matchT_, &fireT_, &suppressT_, &blockedT_})
                while (!q->empty() && q->front() < ts - kScopeSeconds) q->pop_front();

            auto& s = stats_;
            s.windowSamples = det.windowSamples();
            s.elapsedSec = ts;
            s.frames = frames;
            s.backlogMs = backlogMs;
            s.maxBacklogMs = maxBacklog;
            s.procUsAvg = procAvg;
            s.procUsMax = procMax;
            s.lastIoiMs = lastIoi;
            s.lastSdMs = lastSd;
            s.value = r.value;
            s.displayValue = r.displayValue;
            s.inSyllable = r.seg.inSyllable;
            s.songActive = r.song.active;
            s.songFraction = r.song.fraction;
            s.songRatio = r.song.ratio;
            s.songFftSize = det.songDetector().fftSize();
            s.songBinHz = det.songDetector().binHz();
            s.songBouts = songBouts;
            s.blocked = blocked;
            s.fbState = r.fbState;
            s.onsets = onsets;
            s.matches = matches;
            s.trials = trials;
            s.fires1 = fires1;
            s.fires2 = fires2;
            s.suppressed = suppressed;
            s.catches = catches;
        }
    }

    io_->writeDigital(DigitalOutputs{});
    eventsCsv_.flush();
    running_ = false;
    std::lock_guard<std::mutex> lk(mx_);
    stats_.running = false;
}

EngineStats Engine::stats() const {
    std::lock_guard<std::mutex> lk(mx_);
    return stats_;
}

void Engine::snapshot(double seconds, ScopeSnapshot& out) const {
    std::lock_guard<std::mutex> lk(mx_);
    const double fs = stats_.sampleRate;
    out.rawRate = fs;
    out.envRate = stats_.frameSamples > 0 ? fs / stats_.frameSamples : 0.0;
    out.tEndSec = stats_.elapsedSec;
    if (raw_.d.empty()) {
        out.raw.clear();
        out.env.clear();
        out.syl.clear();
        return;
    }
    raw_.copyLast(size_t(seconds * fs), out.raw);
    env_.copyLast(size_t(seconds * out.envRate), out.env);
    syl_.copyLast(size_t(seconds * out.envRate), out.syl);
    songFrac_.copyLast(size_t(seconds * out.envRate), out.songFrac);
    songRatio_.copyLast(size_t(seconds * out.envRate), out.songRatio);
    songOn_.copyLast(size_t(seconds * out.envRate), out.songOn);
    out.matchT.assign(matchT_.begin(), matchT_.end());
    out.blockedT.assign(blockedT_.begin(), blockedT_.end());
    out.fireT.assign(fireT_.begin(), fireT_.end());
    out.suppressT.assign(suppressT_.begin(), suppressT_.end());
}

std::vector<LogEvent> Engine::events(size_t max) const {
    std::lock_guard<std::mutex> lk(mx_);
    const size_t n = std::min(max, events_.size());
    return std::vector<LogEvent>(events_.end() - std::ptrdiff_t(n), events_.end());
}

std::string Engine::error() const {
    std::lock_guard<std::mutex> lk(mx_);
    return error_;
}

}  // namespace fp
