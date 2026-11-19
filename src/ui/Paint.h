#pragma once

#include "ui/Canvas.h"

#include <string>

namespace ghovlz {

class Engine;
class Project;

enum class View { Arrangement, Session, Perform, Mixer, Device, Piano, Browser, Plugin };

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
};

void paintFrame(Canvas& canvas, const Frame& frame);

// Top-bar hit targets, in the same coordinates paintFrame uses.
enum class Hit { None, Arr, Ses, Perf, Mix, Play, Stop, Rec };

Hit hitTest(float x, float y, int width, int height);
View viewFromHit(Hit hit, View current);

} // namespace ghovlz
