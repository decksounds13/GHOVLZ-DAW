#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ghovlz {

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    static Color argb(uint32_t v) {
        return Color{uint8_t((v >> 16) & 255), uint8_t((v >> 8) & 255), uint8_t(v & 255), uint8_t((v >> 24) & 255)};
    }
    Color withAlpha(uint8_t na) const { return Color{r, g, b, na}; }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct Image {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
};

Image loadImageRgba(const std::string& path);

// Drawing surface shared by the software raster backend and Direct2D.
// Coordinates are device pixels. A later Skia or Vulkan backend can implement the same calls.
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual const char* backendName() const = 0;

    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void clear(Color color) = 0;
    virtual void fillRect(Rect r, Color color) = 0;
    virtual void strokeRect(Rect r, Color color, float thickness) = 0;
    virtual void fillRoundRect(Rect r, float radius, Color color) = 0;
    virtual void fillEllipse(float cx, float cy, float rx, float ry, Color color) = 0;
    virtual void line(float x0, float y0, float x1, float y1, Color color, float thickness) = 0;
    virtual void arc(float cx, float cy, float radius, float a0, float a1, Color color, float thickness) = 0;
    virtual void clip(Rect r) = 0;
    virtual void resetClip() = 0;
    virtual void drawText(const std::string& text, float x, float y, float size, Color color, bool bold) = 0;
    virtual float textWidth(const std::string& text, float size, bool bold) const = 0;
    virtual void dropShadow(Rect r, float radius, float blur, Color color) = 0;
    virtual void blurRect(const std::string& cacheKey, Rect r, float sigma) = 0;
    virtual void drawImage(const Image& image, Rect src, Rect dst) = 0;
    virtual bool savePng(const std::string& path) = 0;
};

std::unique_ptr<Canvas> makeSoftwareCanvas(int width, int height);
const uint8_t* softwarePixels(Canvas& canvas);

} // namespace ghovlz
