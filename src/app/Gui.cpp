#include "app/Gui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "app/Widgets.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "implot.h"

namespace fp {

using namespace ui;

namespace {

ImVec4 stateColor(FbState s) {
    switch (s) {
        case FbState::Listening: return kGreen;
        case FbState::RefractoryPeriod: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        case FbState::SendingTrigger1:
        case FbState::SendingTrigger2: return kRed;
        default: return kAmber;
    }
}

}  // namespace

Gui::Gui(std::string settingsPath) : settingsPath_(std::move(settingsPath)) {
    if (std::filesystem::exists(settingsPath_)) {
        std::string err;
        if (loadParams(settingsPath_, p_, &err))
            status_ = "Loaded " + settingsPath_;
        else
            status_ = "Settings: " + err;
    }
    if (!daqAvailable()) p_.source.kind = SourceKind::Simulated;
}

Gui::~Gui() {
    engine_.stop();
    saveParams(settingsPath_, p_);
}

void Gui::startEngine() {
    std::string err;
    if (engine_.start(p_, err))
        status_ = "Running";
    else
        status_ = "Start failed: " + err;
}

void Gui::openTune(const std::string& folder, bool analyze, bool summary) {
    tune_.openFolder(folder, analyze || summary, p_, summary);
    selectTuneTab_ = true;
}

void Gui::pushParams() {
    if (engine_.running()) engine_.updateParams(p_);
}

void Gui::draw() {
    changed() = false;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    drawToolbar();
    ImGui::Separator();

    ImGui::BeginChild("controls", ImVec2(31.0f * em(), 0), ImGuiChildFlags_Border);
    drawControls();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("right");
    if (ImGui::BeginTabBar("views")) {
        if (ImGui::BeginTabItem("Live")) {
            drawStatus();
            const float h = ImGui::GetContentRegionAvail().y;
            drawPlots(h * 0.80f);
            drawEventLog();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Tune on recordings", nullptr, selectTuneTab_ ? ImGuiTabItemFlags_SetSelected : 0)) {
            tune_.draw(p_);
            ImGui::EndTabItem();
        }
        selectTuneTab_ = false;
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::End();

    if (changed()) pushParams();
}

void Gui::drawToolbar() {
    const bool running = engine_.running();
    // The engine stops itself on a fatal read error; surface it once.
    const std::string err = engine_.error();
    if (!running && !err.empty() && status_.rfind("Stopped", 0) != 0) status_ = "Stopped: " + err;

    ImGui::PushStyleColor(ImGuiCol_Button, running ? ImVec4(0.65f, 0.15f, 0.15f, 1) : ImVec4(0.15f, 0.5f, 0.2f, 1));
    if (ImGui::Button(running ? "  Stop  " : "  Start  ", ImVec2(6 * em(), 0))) {
        if (running) {
            engine_.stop();
            status_ = "Stopped";
        } else {
            startEngine();
        }
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::BeginDisabled(running);
    EnumCombo("Source", &p_.source.kind, 2, 16.0f);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Test feedback 1")) engine_.manualFire(1);
    ImGui::SameLine();
    if (ImGui::Button("Test feedback 2")) engine_.manualFire(2);
    ImGui::SameLine();
    if (ImGui::Button("Reset counters")) engine_.resetCounters();
    ImGui::EndDisabled();

    // Settings files, e.g. one per bird. The file in use is saved again on exit.
    const char* iniFilter = "Settings (*.ini)\0*.ini\0All files\0*.*\0";
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string e;
        status_ = saveParams(settingsPath_, p_, &e) ? "Saved " + settingsPath_ : e;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as...")) {
        const std::string f = saveFileDialog(iniFilter, "ini");
        std::string e;
        if (!f.empty()) {
            if (saveParams(f, p_, &e)) {
                settingsPath_ = f;
                status_ = "Saved " + f;
            } else {
                status_ = e;
            }
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Load...")) {
        const std::string f = openFileDialog(iniFilter);
        if (!f.empty()) {
            AppParams np = p_;
            std::string e;
            if (loadParams(f, np, &e)) {
                p_ = np;
                settingsPath_ = f;
                status_ = "Loaded " + f;
            } else {
                status_ = "Load failed: " + e;
            }
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const bool bad = status_.find("fail") != std::string::npos || status_.rfind("Stopped:", 0) == 0;
    ImGui::TextColored(bad ? kRed : ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", status_.c_str());
}

void Gui::drawControls() {
    const ImGuiTreeNodeFlags open = ImGuiTreeNodeFlags_DefaultOpen;
    if (ImGui::CollapsingHeader("Spectrum Selection", open)) drawSpectrum();
    if (ImGui::CollapsingHeader("Processing Params", open)) drawProcessing();
    if (ImGui::CollapsingHeader("Target Sequence", open)) drawSequence();
    if (ImGui::CollapsingHeader("Song Detection (DetectBirdSong)", open)) drawSong();
    if (ImGui::CollapsingHeader("Feedback", open)) drawFeedback();
    if (ImGui::CollapsingHeader("Hardware / Source")) drawHardware();
    if (ImGui::CollapsingHeader("Recording")) drawRecording();
    if (ImGui::CollapsingHeader("Display")) drawDisplay();
}

void Gui::drawHardware() {
    const bool running = engine_.running();
    if (running) ImGui::TextColored(kAmber, "Stop acquisition to change hardware settings.");
    if (!daqAvailable()) ImGui::TextColored(kRed, "Built without NI-DAQmx: simulated source only.");
    static const std::string devices = describeDaqDevices();  // queried once
    ImGui::TextWrapped("Devices: %s", devices.c_str());
    ImGui::BeginDisabled(running);

    ImGui::SeparatorText("Analog input (microphone)");
    InputStr("AI channel", &p_.hw.aiChannel, 10);
    EnumCombo("Terminal config", &p_.hw.terminalConfig, 5, 10);
    InputD("Min (V)", &p_.hw.aiMin, "%.3g", 4);
    ImGui::SameLine();
    InputD("Max (V)", &p_.hw.aiMax, "%.3g", 4);
    InputD("Sample rate (Hz)", &p_.hw.sampleRate, "%.0f");
    InputI("Samples per frame", &p_.hw.samplesPerFrame);
    p_.hw.samplesPerFrame = std::max(1, p_.hw.samplesPerFrame);
    ImGui::SameLine();
    ImGui::TextDisabled("= %.2f ms", p_.hw.samplesPerFrame * 1000.0 / std::max(1.0, p_.hw.sampleRate));
    InputD("Input buffer (s)", &p_.hw.inputBufferSec, "%.2g");

    ImGui::SeparatorText("Analog output (feedback waveform)");
    InputStr("AO channel", &p_.hw.aoChannel, 10);
    HelpMarker("Plays Feedback 1/2 wave files. Leave empty to disable. The AO rate is the wave file's sample rate.");
    InputD("AO min (V)", &p_.hw.aoMin, "%.3g", 4);
    ImGui::SameLine();
    InputD("AO max (V)", &p_.hw.aoMax, "%.3g", 4);
    InputD("Volts at full scale", &p_.hw.aoScale, "%.4g");
    HelpMarker("A wave sample of +1.0 is output as this many volts (clipped to the AO range).");

    ImGui::SeparatorText("Digital outputs (TTL)");
    InputStr("Feedback line", &p_.hw.doFeedbackLine, 12);
    HelpMarker("Pulses for 'TTL pulse' ms each time feedback is delivered. One line per field, e.g. Dev1/port0/line0. Empty = off.");
    InputStr("Syllable line", &p_.hw.doSyllableLine, 12);
    HelpMarker("High while a syllable is detected (start/end thresholds). Useful as a sync signal for the recording system.");
    InputStr("Match line", &p_.hw.doMatchLine, 12);
    HelpMarker("Pulses when the target sequence matches, whether or not feedback follows (catch trials, refractory, no song).");
    InputStr("Song line", &p_.hw.doSongLine, 12);
    HelpMarker("High while the song detector says the bird is singing. Empty = off.");

    ImGui::SeparatorText("Simulated source");
    PathInput("Song WAV", &p_.source.simWavPath);
    HelpMarker("Played back in real time, looped. Leave empty for a built-in synthetic song that matches the default setpoints.");
    InputD("Synthetic noise (V)", &p_.source.simNoiseVolts, "%.4g");

    ImGui::EndDisabled();
}

void Gui::drawSpectrum() {
    InputD("# previous ms included in calcs", &p_.spectrum.windowMs, "%.3g");
    p_.spectrum.windowMs = std::clamp(p_.spectrum.windowMs, 1.0, 200.0);
    const double fs = engine_.running() ? engine_.stats().sampleRate : p_.hw.sampleRate;
    const double n = std::round(p_.spectrum.windowMs * fs / 1000.0);
    ImGui::TextDisabled("%.0f samples, %.1f Hz bins", n, fs / std::max(1.0, n));

    auto bandTable = [&](const char* id, std::array<Band, kBandsPerGroup>& bands, const ImVec4& color) {
        if (!ImGui::BeginTable(id, 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) return;
        ImGui::TableSetupColumn("Gain");
        ImGui::TableSetupColumn("Low Hz");
        ImGui::TableSetupColumn("High Hz");
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        for (int i = 0; i < kBandsPerGroup; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            InputD("##g", &bands[i].gain, "%.4g", 5);
            ImGui::TableNextColumn();
            InputD("##lo", &bands[i].lowHz, "%.0f", 5);
            ImGui::TableNextColumn();
            InputD("##hi", &bands[i].highHz, "%.0f", 5);
            ImGui::PopID();
        }
        ImGui::EndTable();
    };
    ImGui::TextColored(kGreen, "Include bands");
    HelpMarker("Band RMS amplitudes are weighted by Gain and summed. Gain 0 disables a row.");
    bandTable("include", p_.spectrum.include, kGreen);
    ImGui::TextColored(kRed, "Subtract bands");
    HelpMarker("Typically low-frequency / cage noise. Combined with the include sum using Band Combination F(x).");
    bandTable("subtract", p_.spectrum.subtract, kRed);

    EnumCombo("Band Combination F(x)", &p_.spectrum.combination, 4, 8);
    InputD("Syl Start Thresh", &p_.spectrum.sylStartThresh, "%.6g");
    InputD("End Thresh", &p_.spectrum.sylEndThresh, "%.6g");
    if (p_.spectrum.sylEndThresh > p_.spectrum.sylStartThresh)
        ImGui::TextColored(kAmber, "End threshold is above start threshold.");
}

void Gui::drawProcessing() {
    Check("Mean Subtract?", &p_.proc.meanSubtract);
    Check("Software Gain?", &p_.proc.softwareGain);
    ImGui::SameLine();
    ImGui::BeginDisabled(!p_.proc.softwareGain);
    InputD("Gain", &p_.proc.gain, "%.4g", 4);
    ImGui::EndDisabled();
    Check("Low Cutoff?", &p_.proc.lowCutoff);
    ImGui::SameLine();
    ImGui::BeginDisabled(!p_.proc.lowCutoff);
    InputD("Low Cutoff Threshold", &p_.proc.lowCutoffThresh, "%.3g", 5);
    ImGui::EndDisabled();
    HelpMarker("Detection signal values below this are set to 0.");
    Check("display dB?", &p_.proc.displayDb);
    HelpMarker("Display only. Thresholds always apply to the linear detection signal.");
}

void Gui::drawSequence() {
    EnumCombo("Trigger Type", &p_.seq.type, 5, 8);
    HelpMarker(
        "Evaluated at each syllable onset (IOI, SD+IOI, IOI+SD+IOI) or offset (SD, IOI+SD).\n"
        "With onsets A, B, C: at C's onset 'IOI' = C-B, 'SD' = duration of B, 'previous IOI' = B-A.\n"
        "A match means C is the target syllable.");
    const auto t = p_.seq.type;
    const bool useIoi = t != TriggerType::SD;
    const bool useSd = t != TriggerType::IOI;
    const bool usePrev = t == TriggerType::IOI_SD_IOI;

    if (ImGui::BeginTable("seq", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Setpoint (ms)");
        ImGui::TableSetupColumn("+/- eps (ms)");
        ImGui::TableHeadersRow();
        auto row = [&](const char* name, double* sp, double* eps, bool used) {
            ImGui::PushID(name);
            ImGui::TableNextRow();
            ImGui::BeginDisabled(!used);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            InputD("##sp", sp, "%.4g", 5);
            ImGui::TableNextColumn();
            InputD("##eps", eps, "%.4g", 5);
            ImGui::EndDisabled();
            ImGui::PopID();
        };
        row("IOI (Setpoint 1)", &p_.seq.ioiSetpointMs, &p_.seq.ioiEpsMs, useIoi);
        row("SD (Setpoint 2)", &p_.seq.sdSetpointMs, &p_.seq.sdEpsMs, useSd);
        row("Prev IOI (Setpoint 3)", &p_.seq.prevIoiSetpointMs, &p_.seq.prevIoiEpsMs, usePrev);
        ImGui::EndTable();
    }
}

void Gui::drawFeedback() {
    Check("Feedback enabled", &p_.fb.enabled);
    HelpMarker("When off, detection and the match TTL keep running but no feedback trials start.");

    if (ImGui::BeginTable("fb", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Feedback 1");
        ImGui::TableSetupColumn("Feedback 2");
        ImGui::TableHeadersRow();
        auto row = [&](const char* name, double* a, double* b, const char* fmt) {
            ImGui::PushID(name);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            InputD("##1", a, fmt, 6);
            ImGui::TableNextColumn();
            InputD("##2", b, fmt, 6);
            ImGui::PopID();
        };
        row("Probability", &p_.fb.prob1, &p_.fb.prob2, "%.3g");
        row("Delay (ms)", &p_.fb.delay1Ms, &p_.fb.delay2Ms, "%.4g");
        row("Detect Signal Thresh", &p_.fb.thresh1, &p_.fb.thresh2, "%.4g");
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Delay 2 = -1 disables feedback 2.");
    PathInput("Wave 1", &p_.fb.wave1, 15);
    PathInput("Wave 2", &p_.fb.wave2, 15);
    if (engine_.running()) ImGui::TextDisabled("Wave files are loaded at Start.");
    EnumCombo("Feedback 2 mode", &p_.fb.mode2, 2, 12);
    HelpMarker(
        "Alternative: each detection gives feedback 1 with Probability 1, else feedback 2 with Probability 2, "
        "else a catch trial.\nFollows feedback 1: after feedback 1 is sent, feedback 2 follows Delay 2 ms later "
        "with Probability 2.");
    InputD("Refractory Period (ms)", &p_.fb.refractoryMs, "%.4g");
    InputD("Feedback Delay (ms)", &p_.fb.extraDelayMs, "%.4g");
    HelpMarker("Added to Delay 1 (and to Delay 2 in Alternative mode).");
    InputD("TTL pulse (ms)", &p_.fb.ttlPulseMs, "%.4g");
}

void Gui::drawSong() {
    auto& s = p_.song;
    Check("Song detector enabled", &s.enabled);
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.enabled);
    Check("Gate feedback on song", &s.gateFeedback);
    HelpMarker("When on, sequence matches only start feedback trials while song is detected. Blocked matches are "
               "logged and shown as grey dashed lines.");

    ImGui::SeparatorText("Song Detect Params");
    InputD("Ratio Threshold", &s.ratioThresh, "%.4g");
    EnumCombo("Ratio", &s.ratioMode, 2, 12);
    HelpMarker("Per spectrogram column: mean power per bin in the bird band divided by the mean power per bin in "
               "the non-bird band (or by bird + non-bird).");
    InputD("Song Duration (s)", &s.songDurationSec, "%.3g");
    InputD("Duration Threshold (0-1)", &s.durationThresh, "%.3g");
    HelpMarker("Song is detected when more than this fraction of the last 'Song Duration' is above the ratio "
               "threshold (LabVIEW: box-kernel convolution > threshold).");
    InputD("Non-bird Freq Cutoff (Hz)", &s.nonBirdCutoffHz, "%.0f");
    InputD("Bird Low Frequency (Hz)", &s.birdLowHz, "%.0f");
    InputD("Bird High Frequency (Hz)", &s.birdHighHz, "%.0f");
    InputD("Min bird-band RMS (V)", &s.minBirdRms, "%.4g");
    HelpMarker("Optional: also require this much signal in the bird band, so quiet broadband hiss can't count as "
               "song. 0 = off (LabVIEW behaviour).");
    InputD("Hold (s)", &s.holdSec, "%.3g");
    HelpMarker("Keep 'song' on this long after the detector drops. 0 = LabVIEW behaviour.");

    ImGui::SeparatorText("Spectrogram");
    InputI("Window length (samples)", &s.windowLength);
    InputI("Frequency bins", &s.frequencyBins);
    InputD("Column hop (ms)", &s.hopMs, "%.3g");
    s.windowLength = std::clamp(s.windowLength, 16, 65536);
    s.hopMs = std::max(0.1, s.hopMs);
    const auto st = engine_.stats();
    if (st.running && st.songFftSize > 0)
        ImGui::TextDisabled("Hann, FFT %d, %.1f Hz bins, column every %.1f ms", st.songFftSize, st.songBinHz, s.hopMs);

    ImGui::SeparatorText("Pre-Processing Params");
    Check("Mean Subtract?##song", &s.meanSubtract);
    InputD("Gain##song", &s.gain, "%.4g", 5);
    ImGui::SameLine();
    InputD("offset##song", &s.offset, "%.4g", 5);
    Check("Butterworth Filter? (Bandpass)", &s.butterworth);
    ImGui::BeginDisabled(!s.butterworth);
    InputI("order", &s.filterOrder, 4);
    s.filterOrder = std::clamp(s.filterOrder, 1, 20);
    InputD("low cutoff (Hz)", &s.filterLowHz, "%.0f");
    InputD("high cutoff (Hz)", &s.filterHighHz, "%.0f");
    ImGui::EndDisabled();
    if (s.butterworth && s.filterLowHz > s.nonBirdCutoffHz)
        ImGui::TextColored(kAmber, "The bandpass removes the non-bird band, so the ratio will be very large.");
    ImGui::EndDisabled();
}

void Gui::drawRecording() {
    InputStr("Output folder", &p_.rec.outputDir, 14);
    Check("Events CSV", &p_.rec.saveEvents);
    Check("Raw microphone WAV", &p_.rec.saveRaw);
    Check("Detection signal WAV (frame rate)", &p_.rec.saveEnvelope);
    ImGui::TextDisabled("Applied at the next Start. A copy of the settings is saved with each session.");
    const auto s = engine_.stats();
    if (!s.sessionPrefix.empty()) ImGui::TextWrapped("Session: %s", s.sessionPrefix.c_str());
}

void Gui::drawDisplay() {
    float w = float(p_.display.windowSec);
    ImGui::SetNextItemWidth(12 * em());
    if (ImGui::SliderFloat("Plot window (s)", &w, 0.2f, 10.0f, "%.1f")) p_.display.windowSec = w;
    ImGui::Checkbox("Raw autoscale Y", &p_.display.rawAutoY);
    if (!p_.display.rawAutoY) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(5 * em());
        ImGui::InputDouble("+/- V", &p_.display.rawYRange, 0, 0, "%.3g");
    }
    ImGui::Checkbox("Detection autoscale Y", &p_.display.envAutoY);
}

void Gui::drawStatus() {
    const auto s = engine_.stats();
    if (!s.running) {
        ImGui::TextDisabled("Not running. Pick a source and press Start.");
        return;
    }
    ImGui::Text("%s  %.0f Hz  frame %d (%.2f ms)  window %d  |  t = %.1f s", s.backend.c_str(), s.sampleRate,
                s.frameSamples, s.frameSamples * 1000.0 / s.sampleRate, s.windowSamples, s.elapsedSec);
    ImGui::SameLine();
    const bool lagging = s.backlogMs > 5.0 * s.frameSamples * 1000.0 / s.sampleRate;
    ImGui::TextColored(lagging ? kRed : ImVec4(0.7f, 0.7f, 0.7f, 1), "|  backlog %.1f ms (max %.1f)  proc %.0f us (max %.0f)",
                       s.backlogMs, s.maxBacklogMs, s.procUsAvg, s.procUsMax);

    ImGui::TextColored(stateColor(s.fbState), "%-18s", toString(s.fbState));
    ImGui::SameLine();
    ImGui::TextColored(s.inSyllable ? kGreen : ImVec4(0.5f, 0.5f, 0.5f, 1), s.inSyllable ? "SYLLABLE" : "silence ");
    ImGui::SameLine();
    ImGui::TextColored(s.songActive ? kBlue : ImVec4(0.5f, 0.5f, 0.5f, 1), s.songActive ? "SONG" : "no song");
    ImGui::SameLine();
    ImGui::Text("| last IOI %.0f ms  SD %.0f ms  | onsets %llu  matches %llu (blocked %llu)  trials %llu  fb1 %llu  "
                "fb2 %llu  suppressed %llu  catch %llu  bouts %llu",
                s.lastIoiMs, s.lastSdMs, (unsigned long long)s.onsets, (unsigned long long)s.matches,
                (unsigned long long)s.blocked, (unsigned long long)s.trials, (unsigned long long)s.fires1,
                (unsigned long long)s.fires2, (unsigned long long)s.suppressed, (unsigned long long)s.catches,
                (unsigned long long)s.songBouts);
}

void Gui::drawPlots(float height) {
    engine_.snapshot(p_.display.windowSec, snap_);
    const double win = p_.display.windowSec;
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    const float plotH = (height - 2 * gap) * 0.36f;
    const float songH = (height - 2 * gap) * 0.28f;

    auto relTimes = [&](const std::vector<double>& t) {
        std::vector<double> r;
        for (double v : t)
            if (v >= snap_.tEndSec - win) r.push_back(v - snap_.tEndSec);
        return r;
    };
    const auto fireX = relTimes(snap_.fireT), matchX = relTimes(snap_.matchT), supX = relTimes(snap_.suppressT);
    const auto blockedX = relTimes(snap_.blockedT);
    auto markers = [&] {
        auto lines = [](const char* name, const std::vector<double>& x, const ImVec4& c, float w) {
            if (x.empty()) return;
            ImPlot::SetNextLineStyle(c, w);
            ImPlot::PlotInfLines(name, x.data(), int(x.size()));
        };
        lines("match", matchX, ImVec4(0.6f, 0.4f, 1.0f, 0.8f), 1.0f);
        lines("blocked (no song)", blockedX, ImVec4(0.5f, 0.5f, 0.5f, 0.9f), 1.0f);
        lines("feedback", fireX, kRed, 2.0f);
        lines("suppressed", supX, ImVec4(0.6f, 0.6f, 0.6f, 0.8f), 1.0f);
    };
    // Frame-rate time axis shared by the detection and song plots.
    auto frameTimes = [&](size_t m) {
        std::vector<double> x(m);
        for (size_t i = 0; i < m; ++i) x[i] = -double(m - 1 - i) / std::max(1.0, snap_.envRate);
        return x;
    };

    // ---- raw microphone voltage
    if (ImPlot::BeginPlot("Microphone", ImVec2(-1, plotH))) {
        ImPlot::SetupAxes(nullptr, "V", 0, p_.display.rawAutoY ? ImPlotAxisFlags_AutoFit : 0);
        ImPlot::SetupAxisLimits(ImAxis_X1, -win, 0, ImPlotCond_Always);
        if (!p_.display.rawAutoY)
            ImPlot::SetupAxisLimits(ImAxis_Y1, -p_.display.rawYRange, p_.display.rawYRange, ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
        const int buckets = std::max(100, int(ImPlot::GetPlotSize().x));
        const size_t nr = snap_.raw.size();
        decimate(snap_.raw.data(), nr, nr > 0 ? -double(nr - 1) / snap_.rawRate : 0.0, snap_.rawRate, buckets, xs_,
                 ys_);
        if (!xs_.empty()) {
            ImPlot::SetNextLineStyle(kBlue, 1.0f);
            ImPlot::PlotLine("mic", xs_.data(), ys_.data(), int(xs_.size()));
        }
        markers();
        ImPlot::EndPlot();
    }

    // ---- detection signal with thresholds and syllable shading
    if (ImPlot::BeginPlot("Detection signal", ImVec2(-1, plotH))) {
        const bool dB = p_.proc.displayDb;
        ImPlot::SetupAxes("time (s)", dB ? "dB" : "amplitude", 0, p_.display.envAutoY ? ImPlotAxisFlags_AutoFit : 0);
        ImPlot::SetupAxis(ImAxis_Y2, nullptr,
                          ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickMarks |
                              ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, 1.0, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_X1, -win, 0, ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);

        const size_t m = snap_.env.size();
        xs_.resize(m);
        ys_.resize(m);
        std::vector<double> syl(m);
        for (size_t i = 0; i < m; ++i) {
            xs_[i] = -double(m - 1 - i) / std::max(1.0, snap_.envRate);
            // In dB mode clamp the floor so silent periods don't squash the plot.
            ys_[i] = dB ? std::max<double>(snap_.env[i], -100.0) : snap_.env[i];
            syl[i] = snap_.syl[i];
        }
        // ImPlot 0.16 reads element 0 even for empty series, so only plot real data.
        if (m > 0) {
            ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
            ImPlot::SetNextFillStyle(kGreen, 0.18f);
            ImPlot::PlotShaded("syllable", xs_.data(), syl.data(), int(m), 0.0);
            ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
            ImPlot::SetNextLineStyle(kAmber, 1.5f);
            ImPlot::PlotLine("signal", xs_.data(), ys_.data(), int(m));
        }
        const double th[2] = {Detector::toDisplay(p_.spectrum.sylStartThresh, dB),
                              Detector::toDisplay(p_.spectrum.sylEndThresh, dB)};
        ImPlot::SetNextLineStyle(kGreen, 1.0f);
        ImPlot::PlotInfLines("start", &th[0], 1, ImPlotInfLinesFlags_Horizontal);
        ImPlot::SetNextLineStyle(kRed, 1.0f);
        ImPlot::PlotInfLines("end", &th[1], 1, ImPlotInfLinesFlags_Horizontal);
        markers();
        ImPlot::EndPlot();
    }

    // ---- song detector: kernel fraction vs. duration threshold, ratio (log) vs. ratio threshold
    if (ImPlot::BeginPlot("Song detector", ImVec2(-1, songH))) {
        ImPlot::SetupAxes("time (s)", "fraction", 0, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.05, ImPlotCond_Always);
        ImPlot::SetupAxis(ImAxis_Y2, "ratio", ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisScale(ImAxis_Y2, ImPlotScale_Log10);
        ImPlot::SetupAxisLimits(ImAxis_X1, -win, 0, ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);

        const size_t m = std::min({snap_.songFrac.size(), snap_.songRatio.size(), snap_.songOn.size()});
        if (m > 0 && p_.song.enabled) {
            const auto x = frameTimes(m);
            std::vector<double> frac(m), ratio(m), on(m);
            for (size_t i = 0; i < m; ++i) {
                frac[i] = snap_.songFrac[i];
                ratio[i] = std::max<double>(snap_.songRatio[i], 1e-6);  // log axis
                on[i] = snap_.songOn[i] * 1.05;
            }
            ImPlot::SetNextFillStyle(kBlue, 0.30f);
            ImPlot::PlotShaded("song", x.data(), on.data(), int(m), 0.0);
            ImPlot::SetNextLineStyle(kBlue, 1.5f);
            ImPlot::PlotLine("fraction", x.data(), frac.data(), int(m));
            const double dth = p_.song.durationThresh;
            ImPlot::SetNextLineStyle(ImVec4(0.35f, 0.6f, 1.0f, 0.6f), 1.0f);
            ImPlot::PlotInfLines("duration thresh", &dth, 1, ImPlotInfLinesFlags_Horizontal);

            ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
            ImPlot::SetNextLineStyle(ImVec4(0.8f, 0.8f, 0.8f, 0.7f), 1.0f);
            ImPlot::PlotLine("ratio", x.data(), ratio.data(), int(m));
            const double rth = p_.song.ratioThresh;
            ImPlot::SetNextLineStyle(ImVec4(0.8f, 0.8f, 0.8f, 0.5f), 1.0f);
            ImPlot::PlotInfLines("ratio thresh", &rth, 1, ImPlotInfLinesFlags_Horizontal);
            ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
        }
        markers();
        ImPlot::EndPlot();
    }
}

void Gui::drawEventLog() {
    const auto ev = engine_.events(300);
    if (!ImGui::BeginTable("events", 3,
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                               ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("time (s)", ImGuiTableColumnFlags_WidthFixed, 6 * em());
    ImGui::TableSetupColumn("event", ImGuiTableColumnFlags_WidthFixed, 7 * em());
    ImGui::TableSetupColumn("detail");
    ImGui::TableHeadersRow();
    for (auto it = ev.rbegin(); it != ev.rend(); ++it) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", it->tMs / 1000.0);
        ImGui::TableNextColumn();
        ImVec4 c(0.8f, 0.8f, 0.8f, 1);
        if (it->type.rfind("feedback", 0) == 0) c = kRed;
        else if (it->type.rfind("song", 0) == 0) c = kBlue;
        else if (it->type == "match") c = ImVec4(0.6f, 0.4f, 1.0f, 1);
        else if (it->type == "error") c = kRed;
        else if (it->type == "onset") c = kGreen;
        ImGui::TextColored(c, "%s", it->type.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(it->detail.c_str());
    }
    ImGui::EndTable();
}

}  // namespace fp
