#pragma once

#include "ui/Canvas.h"

#include <memory>

namespace ghovlz {

// HWND is passed as void* so this header stays free of the Windows headers.
// A null window renders to an offscreen bitmap, which is what screenshots read back.
// forceWarp skips the hardware device. Returns null when the requested device cannot be created.
std::unique_ptr<Canvas> makeDirect2DCanvas(void* hwnd, int width, int height, bool forceWarp = false);
void resizeDirect2DCanvas(Canvas& canvas, int width, int height);
void setDirect2DDpi(Canvas& canvas, int dpi);
inline int dipExtent(int pixels, int dpi) { return pixels * 96 / (dpi < 96 ? 96 : dpi); }

} // namespace ghovlz
