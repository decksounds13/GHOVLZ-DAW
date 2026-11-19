#pragma once

#include "ui/Canvas.h"

#include <memory>

namespace ghovlz {

// Auto is a hardware D3D11 device, then WARP, then the software canvas.
// Warp skips the hardware device. Software never opens Direct2D.
// Linux always uses the software canvas. A null window is an offscreen target.
enum class RendererPreference { Auto, Warp, Software };

struct RendererDesc {
    void* window = nullptr;
    int width = 1680;
    int height = 980;
    RendererPreference preference = RendererPreference::Auto;
};

std::unique_ptr<Canvas> openRenderer(const RendererDesc& desc);

} // namespace ghovlz
