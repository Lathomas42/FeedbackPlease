#pragma once
// "Tune" tab: the C++ version of LabVIEW's Kernel Extraction tab. Browse a folder of
// WAV files, see each file's spectrogram, detection signal and song detector, drag
// thresholds / band edges and watch detection update, Ctrl+click a syllable to make
// it the target, and analyze the whole folder to see how settings perform across
// all of a bird's song.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Offline.h"
#include "core/Params.h"

namespace fp {

struct Spectrogram {
    int cols = 0, rows = 0;   // rows span 0 .. fMax
    double t0 = 0.0, t1 = 0.0;  // time of first / last column centre (s)
    double fMax = 0.0;
    std::vector<float> db;    // row-major, row 0 = lowest frequency
    float dbMax = 0.0f;
};

struct FileAnalysis {
    std::string path;
    std::string error;
    double sampleRate = 0.0;
    double durationSec = 0.0;
    std::shared_ptr<const std::vector<float>> audio;
    std::shared_ptr<const Spectrogram> spec;
    std::shared_ptr<const EnvelopeTrack> env;
    std::shared_ptr<const SongTrack> song;
    uint64_t specKey = 0, envKey = 0, songKey = 0;
};

// Analyzes the file being viewed on a background thread. A new request cancels
// the work in progress; finished stages are reused when only later stages change.
class FileAnalyzer {
public:
    ~FileAnalyzer();
    void request(const std::string& path, const AppParams& p, int specWindow, double specMaxHz);
    std::shared_ptr<const FileAnalysis> result() const;
    bool busy() const { return busy_.load(); }
    float progress() const { return progress_.load(); }

private:
    struct Job {
        std::string path;
        AppParams p;
        int specWindow = 512;
        double specMaxHz = 10000.0;
    };
    void run();
    void publish(const std::shared_ptr<FileAnalysis>& a);

    mutable std::mutex mx_;
    std::condition_variable cv_;
    std::thread th_;
    bool hasJob_ = false, stop_ = false;
    Job job_;
    std::shared_ptr<const FileAnalysis> result_;
    std::atomic<bool> cancel_{false}, busy_{false};
    std::atomic<float> progress_{0.0f};
};

struct BatchEntry {
    std::string path;
    double durationSec = 0.0;
    std::shared_ptr<const EnvelopeTrack> env;
    std::shared_ptr<const SongTrack> song;
    std::string error;
};

// Computes envelope + song tracks for every file in the folder.
class BatchAnalyzer {
public:
    ~BatchAnalyzer();
    void start(const std::vector<std::string>& files, const AppParams& p);
    void cancel();
    bool running() const { return running_.load(); }
    float progress() const { return progress_.load(); }
    // Entries finished so far, and the settings fingerprints they were computed with.
    void results(std::vector<BatchEntry>& out, uint64_t& envKey, uint64_t& songKey) const;
    size_t count() const;

private:
    void run(std::vector<std::string> files, AppParams p);
    mutable std::mutex mx_;
    std::thread th_;
    std::vector<BatchEntry> entries_;
    uint64_t envKey_ = 0, songKey_ = 0;
    std::atomic<bool> running_{false}, cancel_{false};
    std::atomic<float> progress_{0.0f}, fileProgress_{0.0f};
};

class TuneView {
public:
    TuneView();
    ~TuneView();
    // Draws the tab. Edits to p set ui::changed().
    void draw(AppParams& p);
    const std::string& folder() const { return folder_; }
    // Opens a folder (and optionally starts the whole-folder analysis), e.g. from the command line.
    void openFolder(const std::string& folder, bool analyze, const AppParams& p, bool showSummary = false);

private:
    void drawBrowser(AppParams& p);
    void drawFileView(AppParams& p);
    void drawFolderSummary(AppParams& p);
    void scanFolder();
    void select(int index);
    void updateRequests(const AppParams& p);
    void updateDecisions(const AppParams& p, const FileAnalysis& a);
    void updateBatchDecisions(const AppParams& p);
    void rebuildTexture(const Spectrogram& s);
    void pickTarget(AppParams& p, double tMs);

    // Folder / files
    std::string folder_;
    std::string pattern_ = "*.wav";
    bool recursive_ = false;
    std::vector<std::string> files_;
    int selected_ = -1;
    std::string scanError_;

    // Current file
    FileAnalyzer analyzer_;
    std::string reqPath_;
    uint64_t reqEnvKey_ = 0, reqSongKey_ = 0, reqSpecKey_ = 0;
    double lastRequestTime_ = -1.0;
    std::shared_ptr<const FileAnalysis> shown_;
    DecisionTrack dec_;
    uint64_t decKey_ = 0;
    const void* decEnv_ = nullptr;
    const void* decSong_ = nullptr;

    // Display
    int specWindow_ = 512;
    double specMaxHz_ = 10000.0;
    float specRange_ = 70.0f, specOffset_ = 0.0f;
    unsigned int tex_ = 0;
    const void* texSpec_ = nullptr;
    float texRange_ = -1.0f, texOffset_ = 0.0f;
    double tMin_ = 0.0, tMax_ = 1.0;
    bool fitX_ = true, fitY_ = true;
    std::string fitPath_;
    bool showBands_ = true, showSongBands_ = false, showSD_ = true, showIOI_ = true, showMatches_ = true,
         showFeedback_ = true;
    bool openFileTab_ = false, openSummaryTab_ = false;
    std::string status_;

    // Folder analysis
    BatchAnalyzer batch_;
    std::vector<BatchEntry> batchEntries_;
    uint64_t batchEnvKey_ = 0, batchSongKey_ = 0;
    struct FileSummary {
        size_t syllables = 0, matches = 0, blocked = 0, fires = 0;
    };
    std::vector<FileSummary> batchSummary_;
    std::vector<SyllableInfo> batchSyllables_;
    uint64_t batchDecKey_ = 0;
    size_t batchDecCount_ = 0;
    double batchDecTime_ = -1.0;
    std::vector<double> scratchX_, scratchY_;
};

}  // namespace fp
