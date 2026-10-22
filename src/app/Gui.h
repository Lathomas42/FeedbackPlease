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
    void drawTarget();
    void drawFeedback();
    void drawSong();
    void drawCodes();
    void drawRecording();
    void drawDisplay();
    void drawStatus();
    void drawPlots(float height);
    void drawEventLog();

    void pushParams();

    AppParams p_;
    std::string settingsPath_;
    Engine engine_;
    EngineStats stats_;  // copied once per frame in draw()
    TuneView tune_;
    std::string status_;
    bool selectTuneTab_ = false;
    int editSyl_ = 0;         // syllable being edited (shared with the Tune tab)
    int lastOpenedSyl_ = -1;  // to open the edited syllable's panel once when it changes
    ScopeSnapshot snap_;
    std::vector<double> xs_, ys_;
};

}  // namespace fp
