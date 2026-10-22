#include "core/WavFile.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace fp {

namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

void wr16(std::FILE* f, uint16_t v) {
    uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)};
    std::fwrite(b, 1, 2, f);
}
void wr32(std::FILE* f, uint32_t v) {
    uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    std::fwrite(b, 1, 4, f);
}

}  // namespace

bool readWav(const std::string& path, WavData& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = path + ": " + m;
        return false;
    };
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);  // UTF-8 / UNC paths
    if (!in) return fail("cannot open");
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        return fail("not a RIFF/WAVE file");

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    size_t dataBytes = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const uint8_t* ck = d.data() + pos;
        const uint32_t size = rd32(ck + 4);
        const size_t body = pos + 8;
        const size_t avail = d.size() - body;
        if (std::memcmp(ck, "fmt ", 4) == 0 && size >= 16 && avail >= 16) {
            format = rd16(ck + 8);
            channels = rd16(ck + 10);
            rate = rd32(ck + 12);
            bits = rd16(ck + 22);
            if (format == 0xFFFE && size >= 40 && avail >= 40) format = rd16(ck + 32);  // WAVE_FORMAT_EXTENSIBLE
        } else if (std::memcmp(ck, "data", 4) == 0) {
            data = ck + 8;
            dataBytes = size <= avail ? size : avail;  // tolerate truncated files
            break;
        }
        pos = body + size + (size & 1);
    }
    if (!data || channels == 0 || rate == 0) return fail("missing fmt or data chunk");
    const bool isFloat = format == 3;
    if (!isFloat && format != 1) return fail("unsupported format tag " + std::to_string(format));
    const size_t bytesPer = bits / 8;
    if (bytesPer == 0) return fail("bad bit depth");
    const size_t frames = dataBytes / (bytesPer * channels);

    out.sampleRate = rate;
    out.samples.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        const uint8_t* s = data + i * bytesPer * channels;  // channel 0
        float v = 0.0f;
        if (isFloat && bits == 32) {
            std::memcpy(&v, s, 4);
        } else if (isFloat && bits == 64) {
            double dv;
            std::memcpy(&dv, s, 8);
            v = float(dv);
        } else if (!isFloat && bits == 8) {
            v = (int(s[0]) - 128) / 128.0f;
        } else if (!isFloat && bits == 16) {
            v = int16_t(rd16(s)) / 32768.0f;
        } else if (!isFloat && bits == 24) {
            int32_t x = int32_t(s[0] | (s[1] << 8) | (s[2] << 16));
            if (x & 0x800000) x |= ~0xFFFFFF;
            v = x / 8388608.0f;
        } else if (!isFloat && bits == 32) {
            v = float(int32_t(rd32(s)) / 2147483648.0);
        } else {
            return fail("unsupported bit depth " + std::to_string(bits));
        }
        out.samples[i] = v;
    }
    return true;
}

WavWriter::~WavWriter() { close(); }

bool WavWriter::open(const std::string& path, double sampleRate, int channels) {
    close();
#ifdef _WIN32
    f_ = _wfopen(std::filesystem::u8path(path).c_str(), L"wb");  // UTF-8 / UNC paths
#else
    f_ = std::fopen(path.c_str(), "wb");
#endif
    if (!f_) return false;
    frames_ = 0;
    channels_ = channels < 1 ? 1 : channels;
    const uint32_t rate = uint32_t(sampleRate + 0.5);
    const uint16_t ch = uint16_t(channels_);
    std::fwrite("RIFF", 1, 4, f_);
    wr32(f_, 0);  // patched on close
    std::fwrite("WAVEfmt ", 1, 8, f_);
    wr32(f_, 16);
    wr16(f_, 3);  // IEEE float
    wr16(f_, ch);
    wr32(f_, rate);
    wr32(f_, rate * 4u * ch);
    wr16(f_, uint16_t(4 * ch));
    wr16(f_, 32);
    std::fwrite("data", 1, 4, f_);
    wr32(f_, 0);  // patched on close
    return true;
}

void WavWriter::write(const float* x, size_t frames) {
    if (!f_ || frames == 0) return;
    std::fwrite(x, sizeof(float), frames * size_t(channels_), f_);
    frames_ += frames;
}

void WavWriter::close() {
    if (!f_) return;
    // RIFF sizes are 32-bit; files over 4 GB (~7 h at 40 kHz) get clamped sizes.
    const uint64_t dataBytes = frames_ * 4u * uint64_t(channels_);
    const uint32_t d32 = dataBytes > 0xFFFFFFF0ull ? 0xFFFFFFF0u : uint32_t(dataBytes);
    std::fseek(f_, 4, SEEK_SET);
    wr32(f_, d32 + 36);
    std::fseek(f_, 40, SEEK_SET);
    wr32(f_, d32);
    std::fclose(f_);
    f_ = nullptr;
}

}  // namespace fp
