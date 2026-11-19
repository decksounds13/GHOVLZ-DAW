#include "ui/Renderer.h"

#if defined(_WIN32)
#include "ui/Direct2DCanvas.h"
#endif

namespace ghovlz {

std::unique_ptr<Canvas> openRenderer(const RendererDesc& desc) {
    const int width = desc.width < 1 ? 1 : desc.width;
    const int height = desc.height < 1 ? 1 : desc.height;
#if defined(_WIN32)
    if (desc.preference != RendererPreference::Software) {
        auto canvas = makeDirect2DCanvas(desc.window, width, height, desc.preference == RendererPreference::Warp);
        if (canvas)
            return canvas;
        if (desc.preference == RendererPreference::Warp)
            return nullptr;
    }
#else
    (void)desc;
#endif
    return makeSoftwareCanvas(width, height);
}

} // namespace ghovlz
