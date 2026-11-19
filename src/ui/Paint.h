#pragma once

#include "ui/Canvas.h"
#include "ui/Theme.h"

#include <string>

namespace ghovlz {

class Engine;
class Project;
class WidgetLayer;

enum class View { Arrangement, Session, Perform, Mixer, Device, Piano, Browser, Plugin, Prefs };

struct Frame {
    const Project* project = nullptr;
    const Engine* engine = nullptr;
    View view = View::Arrangement;
    bool playing = false;
    bool recording = false;
    double playhead = 0;
    float cpu = 0;
    const char* backend = "CPU";
    const char* audioBackend = "Off";
    std::string midiPort;
    float pluginGain = 0.5f;
    bool pluginLoaded = false;
    const char* quantize = "1/16";
    int sessionScene = 3;
    std::string search;
    std::string prefBackend = "WASAPI shared";
    std::string prefDevice = "Default";
    int prefRate = 48000;
    int prefFrames = 256;
    std::string prefRenderer = "Auto";
    float prefScale = 1.f;
    std::string prefMessage;
    // UI thread only. Null uses the GHOVLZ Gold built-in. The audio thread never reads this.
    const Theme* theme = nullptr;
    // Filled by paintFrame. Null skips hit targets.
    WidgetLayer* widgets = nullptr;
};

void paintFrame(Canvas& canvas, const Frame& frame);

// One stereo meter, for the built-in profile renders. Columns use peakL and peakR separately.
void paintMeterCard(Canvas& canvas, const Theme& theme, float peakL, float peakR, float rmsL, float rmsR,
                    float holdL, float holdR, bool clip);

// Top-bar hit targets, in the same coordinates paintFrame uses.
// `scale` is the paint scale: a 200% play button is the 100% rect times 2, within 1 px.
enum class Hit { None, Arr, Ses, Perf, Mix, Play, Stop, Rec, Loop, Metro, Badge };

Rect hitRect(Hit hit, float scale = 1.f);
Hit hitTest(float x, float y, int width, int height, float scale = 1.f);
View viewFromHit(Hit hit, View current);

} // namespace ghovlz
