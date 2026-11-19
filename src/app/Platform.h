#pragma once

#include "engine/Engine.h"
#include "model/Project.h"
#include "ui/Paint.h"

#include <memory>

namespace ghovlz {

struct AppHost {
    Engine* engine = nullptr;
    std::shared_ptr<Project> project;
    Frame frame;
};

// Opens a window and returns when the user closes it. Linux uses the software
// canvas and X11. Windows uses Direct2D on D3D11, then WARP, then the software canvas.
int runWindow(AppHost& host);

} // namespace ghovlz
