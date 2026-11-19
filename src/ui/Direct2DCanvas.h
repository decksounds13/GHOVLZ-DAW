#pragma once

#include "ui/Canvas.h"

#include <memory>

namespace ghovlz {

// HWND is passed as void* so this header stays free of the Windows headers.
// Returns null when neither a hardware D3D11 device nor WARP can be created.
std::unique_ptr<Canvas> makeDirect2DCanvas(void* hwnd, int width, int height);
void resizeDirect2DCanvas(Canvas& canvas, int width, int height);
void setDirect2DDpi(Canvas& canvas, int dpi);
inline int dipExtent(int pixels, int dpi) { return pixels * 96 / (dpi < 96 ? 96 : dpi); }

} // namespace ghovlz
