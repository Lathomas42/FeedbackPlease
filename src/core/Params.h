#pragma once
// All user-tunable settings. Field names mirror the LabVIEW front panel so the
// two can be compared side by side; defaults are the values from that panel.

#include <array>
#include <cstdint>
#include <string>

namespace fp {

// One row of the "Spectrum Selection" band table: Gain, High Freq, Low Freq.
struct Band {
    double gain = 0.0;  // linear weight applied to the band amplitude; 0 disables the band
    double lowHz = 0.0;
    double highHz = 0.0;
};

enum class BandCombination : int { Difference = 0, Sum = 1, Ratio = 2, IncludeOnly = 3 };
enum class TriggerType : int { IOI = 0, SD = 1, IOI_SD = 2, SD_IOI = 3, IOI_SD_IOI = 4 };
// How "Feedback 2" relates to "Feedback 1" (the LabVIEW panel is ambiguous, see README).
enum class Feedback2Mode : int { Alternative = 0, AfterFeedback1 = 1 };
enum class SourceKind : int { Daq = 0, Simulated = 1 };
// DAQmx terminal configuration (index into the GUI combo; mapped in DaqBackend).
enum class TerminalConfig : int { Default = 0, RSE = 1, NRSE = 2, Differential = 3, PseudoDiff = 4 };

constexpr int kBandsPerGroup = 4;

struct HardwareParams {
    std::string aiChannel = "Dev1/ai0";
    TerminalConfig terminalConfig = TerminalConfig::Default;
    double aiMin = -10.0;
    double aiMax = 10.0;
    double sampleRate = 40000.0;
    int samplesPerFrame = 40;     // 40 samples @ 40 kHz = 1 ms detection update
    double inputBufferSec = 2.0;  // DAQmx host buffer; only matters if we fall behind

    std::string aoChannel = "Dev1/ao0";  // feedback waveform output; empty = no AO
    double aoMin = -10.0;
    double aoMax = 10.0;
    double aoScale = 1.0;  // volts output for a full-scale (+/-1.0) sample in the wave file

    // Digital lines; any may be empty to disable it.
    std::string doFeedbackLine = "Dev1/port0/line0";  // pulses when feedback is delivered
    std::string doSyllableLine = "Dev1/port0/line1";  // high while a syllable is detected
    std::string doMatchLine = "Dev1/port0/line2";     // pulses when the target sequence matches
    std::string doSongLine;                           // high while song is detected (off by default)
};

struct SourceParams {
    SourceKind kind = SourceKind::Daq;
    std::string simWavPath;  // empty = built-in synthetic song
    double simNoiseVolts = 0.0005;
};

// "Spectrum Selection" cluster.
struct SpectrumParams {
    double windowMs = 15.0;  // "# previous ms included in calcs"
    // Green rows on the panel: bands that are summed into the detection signal.
    std::array<Band, kBandsPerGroup> include{{{0, 1500, 2500}, {20, 1000, 7000}, {0, 0, 0}, {0, 0, 0}}};
    // Red rows on the panel: bands combined against the green ones (noise bands).
    std::array<Band, kBandsPerGroup> subtract{{{0, 500, 1500}, {1, 2, 1000}, {0, 0, 0}, {0, 0, 0}}};
    BandCombination combination = BandCombination::Difference;
    double sylStartThresh = 0.069296;
    double sylEndThresh = 0.0274191;
};

// "Processing Params" cluster.
struct ProcessingParams {
    bool meanSubtract = true;
    bool lowCutoff = false;
    double lowCutoffThresh = 1e-7;
    bool softwareGain = true;
    double gain = 3.0;
    bool displayDb = true;  // display only; detection always uses the linear value
};

// Trigger-type and setpoint part of "Feedback Params" (CalcDetectSignal.vi).
// Roles follow the wiring in CalcDetectSignal: Setpoint 1 is checked against the
// IOI that just ended, Setpoint 2 against the most recent syllable duration and
// Setpoint 3 against the IOI before that.
struct SequenceParams {
    TriggerType type = TriggerType::IOI_SD_IOI;
    double ioiSetpointMs = 124.0;  // "Setpoint"
    double ioiEpsMs = 10.0;        // "eps"
    double sdSetpointMs = 95.0;    // "Setpoint 2"
    double sdEpsMs = 10.0;         // "Eps 2"
    double prevIoiSetpointMs = 95.0;  // "Setpoint 3"
    double prevIoiEpsMs = 10.0;       // "eps 3"
};

// Feedback state machine part of "Feedback Params".
struct FeedbackParams {
    double prob1 = 1.0;
    double delay1Ms = 20.0;
    double thresh1 = 0.0001;  // "Detect Signal Thresh. 1": envelope must still be >= this when sending
    std::string wave1;

    double prob2 = 0.0;
    double delay2Ms = -1.0;  // -1 = unused
    double thresh2 = 0.0;
    std::string wave2;

    Feedback2Mode mode2 = Feedback2Mode::Alternative;
    double refractoryMs = 100.0;
    double extraDelayMs = 0.0;  // "Feedback Delay (ms)", added to Delay 1/2
    double ttlPulseMs = 5.0;    // width of the feedback / match TTL pulses
    bool enabled = true;        // master switch; detection still runs when off
};

enum class SongRatioMode : int { BirdOverLow = 0, BirdOverTotal = 1 };

// DetectBirdSong.vi: "Song Detect 5 Params" + "Pre-Processing Params".
// Separates song from calls and cage noise; when gateFeedback is on, sequence
// matches only start feedback trials while song is detected.
struct SongParams {
    bool enabled = true;
    bool gateFeedback = true;

    // Pre-processing (applied to a copy of the signal used only by the song detector)
    bool meanSubtract = true;
    double gain = 1.0;
    double offset = 0.0;
    bool butterworth = false;
    int filterOrder = 6;
    double filterLowHz = 3000.0;
    double filterHighHz = 10000.0;

    // Spectrogram
    int windowLength = 2048;  // "window info: length" (Hanning)
    int frequencyBins = 512;  // FFT size = max(window length, 2 * bins), rounded up to a power of 2
    double hopMs = 5.0;       // time between spectrogram columns (replaces "time steps")

    // Decision
    double nonBirdCutoffHz = 1000.0;
    double birdLowHz = 3000.0;
    double birdHighHz = 10000.0;
    SongRatioMode ratioMode = SongRatioMode::BirdOverLow;
    double ratioThresh = 0.1;
    double minBirdRms = 0.0;      // optional level gate (V RMS in the bird band); 0 = off
    double songDurationSec = 0.5; // length of the box kernel
    double durationThresh = 0.25; // fraction of the kernel that must be above the ratio threshold
    double holdSec = 0.0;         // keep "song" on this long after the detector drops
};

struct DisplayParams {
    double windowSec = 2.0;
    bool rawAutoY = true;
    double rawYRange = 1.0;
    bool envAutoY = true;
};

struct RecordingParams {
    std::string outputDir = "recordings";
    bool saveEvents = true;    // CSV of onsets/offsets/matches/feedback
    bool saveRaw = false;      // raw microphone WAV (float32)
    bool saveEnvelope = false; // detection signal WAV at the frame rate
};

struct AppParams {
    HardwareParams hw;
    SourceParams source;
    SpectrumParams spectrum;
    ProcessingParams proc;
    SequenceParams seq;
    FeedbackParams fb;
    SongParams song;
    DisplayParams display;
    RecordingParams rec;
};

// Simple "key = value" text format. Unknown keys are ignored and missing keys keep
// their current values, so old settings files keep loading after fields are added.
bool loadParams(const std::string& path, AppParams& p, std::string* err = nullptr);
bool saveParams(const std::string& path, const AppParams& p, std::string* err = nullptr);

const char* toString(BandCombination c);
const char* toString(TriggerType t);
const char* toString(Feedback2Mode m);
const char* toString(SourceKind k);
const char* toString(TerminalConfig t);
const char* toString(SongRatioMode m);

}  // namespace fp
