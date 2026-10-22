#include "core/SongRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fp {

bool SongFileWriter::open(const SongFileConfig& cfg, std::string* err) {
    close();
    cfg_ = cfg;
    ch_ = int(cfg.mic) + int(cfg.detection) + int(cfg.markers);
    if (ch_ == 0) {
        if (err) *err = "song-triggered recording has no channels selected";
        return false;
    }
    preN_ = size_t(std::max(0.0, cfg.preRollSec) * cfg.sampleRate);
    postN_ = size_t(std::max(0.0, cfg.postRollSec) * cfg.sampleRate);
    ring_.assign(preN_ * size_t(ch_), 0.0f);
    ringHead_ = ringCount_ = 0;
    sample_ = fileStart_ = lastSong_ = 0;
    fileNo_ = 0;
    filesDone_ = 0;
    recording_ = false;

    index_.open(std::filesystem::u8path(cfg.prefix + "_songs.csv"));
    if (!index_) {
        if (err) *err = "cannot write " + cfg.prefix + "_songs.csv";
        return false;
    }
    index_ << "file,start_sample,end_sample,start_s,duration_s,pre_roll_s,channels\n";
    open_ = true;
    return true;
}

void SongFileWriter::push(const float* mic, int n, float detection, float marker, bool song) {
    if (!open_ || n <= 0) return;

    // Interleave this frame: mic, detection, markers (whichever are enabled).
    frame_.resize(size_t(n) * size_t(ch_));
    for (int i = 0; i < n; ++i) {
        float* s = &frame_[size_t(i) * size_t(ch_)];
        int c = 0;
        if (cfg_.mic) s[c++] = mic[i];
        if (cfg_.detection) s[c++] = detection;
        if (cfg_.markers) s[c++] = marker;
    }

    if (!recording_ && song) startFile();  // writes the pre-roll history first
    if (recording_) {
        wav_.write(frame_.data(), size_t(n));
        const uint64_t end = sample_ + uint64_t(n);
        if (song)
            lastSong_ = end;
        else if (end - lastSong_ >= postN_)
            endFile();
    }

    // Keep the ring up to date whether or not a file is open, so a song that starts
    // soon after a file closes still gets its full pre-roll.
    if (preN_ > 0) {
        for (int i = 0; i < n; ++i) {
            std::copy_n(&frame_[size_t(i) * size_t(ch_)], ch_, &ring_[ringHead_ * size_t(ch_)]);
            if (++ringHead_ == preN_) ringHead_ = 0;
        }
        ringCount_ = std::min(preN_, ringCount_ + size_t(n));
    }
    sample_ += uint64_t(n);
}

void SongFileWriter::startFile() {
    char name[32];
    std::snprintf(name, sizeof name, "_song%03d.wav", fileNo_ + 1);
    fileName_ = cfg_.prefix + name;
    if (!wav_.open(fileName_, cfg_.sampleRate, ch_)) return;  // disk problem: stay idle, try again next song
    ++fileNo_;
    // History, oldest first: the ring's valid part ends just before ringHead_.
    if (ringCount_ > 0) {
        const size_t start = (ringHead_ + preN_ - ringCount_) % preN_;
        const size_t firstPart = std::min(ringCount_, preN_ - start);
        wav_.write(&ring_[start * size_t(ch_)], firstPart);
        wav_.write(ring_.data(), ringCount_ - firstPart);
    }
    preInFile_ = ringCount_;
    fileStart_ = sample_ - ringCount_;
    recording_ = true;
}

void SongFileWriter::endFile() {
    if (!recording_) return;
    wav_.close();
    recording_ = false;
    const uint64_t end = sample_ + uint64_t(frame_.size() / size_t(ch_));  // end of the frame just written
    const double fs = cfg_.sampleRate;
    index_ << std::filesystem::u8path(fileName_).filename().u8string() << ',' << fileStart_ << ',' << end << ','
           << double(fileStart_) / fs << ',' << double(end - fileStart_) / fs << ',' << double(preInFile_) / fs << ','
           << ch_ << '\n';
    index_.flush();
    ++filesDone_;
}

void SongFileWriter::close() {
    if (!open_) return;
    if (recording_) {
        // Called between frames: the last frame has already been counted in sample_.
        frame_.clear();
        endFile();
    }
    index_.close();
    open_ = false;
}

}  // namespace fp
