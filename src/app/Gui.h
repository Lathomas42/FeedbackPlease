#pragma once

#include <string>
#include <vector>

#include "app/Engine.h"
#include "app/TuneView.h"
#include "core/Params.h"

namespace fp {

class Gui {
public:
    explicit Gui(std::string settingsPath);
    ~Gui();
    void draw();  // one ImGui frame
    void startEngine();
    void openTune(const std::string& folder, bool analyze, bool summary);

private:
    void drawToolbar();
    void drawControls();
    void drawHardware();
    void drawSpectrum();
    void drawProcessing();
    void drawSequence();
    void drawFeedback();
    void drawSong();
    void drawRecording();
    void drawDisplay();
    void drawStatus();
    void drawPlots(float height);
    void drawEventLog();

    void pushParams();

    AppParams p_;
    std::string settingsPath_;
    Engine engine_;
    TuneView tune_;
    std::string status_;
    bool selectTuneTab_ = false;
    ScopeSnapshot snap_;
    std::vector<double> xs_, ys_;
};

}  // namespace fp
