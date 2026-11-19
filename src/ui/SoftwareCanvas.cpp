#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_truetype.h"

#include "ui/Canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace ghovlz {
namespace {

struct FontFile {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    bool ok = false;
};

FontFile loadFont(const std::string& path) {
    FontFile f;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return f;
    f.bytes.assign(std::istreambuf_iterator<char>(in), {});
    if (f.bytes.empty())
        return f;
    f.ok = stbtt_InitFont(&f.info, f.bytes.data(), 0) != 0;
    return f;
}

int blurRadius(float sigma) { return std::max(1, static_cast<int>(std::lround(sigma))); }

void boxBlur(std::vector<float>& src, int w, int h, int radius) {
    if (radius < 1 || w < 1 || h < 1)
        return;
    std::vector<float> tmp(src.size());
    const int span = radius * 2 + 1;
    for (int y = 0; y < h; ++y) {
        float acc = 0.f;
        for (int i = -radius; i <= radius; ++i) {
            int x = std::clamp(i, 0, w - 1);
            acc += src[static_cast<size_t>(y * w + x)];
        }
        for (int x = 0; x < w; ++x) {
            tmp[static_cast<size_t>(y * w + x)] = acc / static_cast<float>(span);
            int x0 = x - radius;
            int x1 = x + radius + 1;
            acc -= src[static_cast<size_t>(y * w + std::clamp(x0, 0, w - 1))];
            acc += src[static_cast<size_t>(y * w + std::clamp(x1, 0, w - 1))];
        }
    }
    for (int x = 0; x < w; ++x) {
        float acc = 0.f;
        for (int i = -radius; i <= radius; ++i) {
            int y = std::clamp(i, 0, h - 1);
            acc += tmp[static_cast<size_t>(y * w + x)];
        }
        for (int y = 0; y < h; ++y) {
            src[static_cast<size_t>(y * w + x)] = acc / static_cast<float>(span);
            int y0 = y - radius;
            int y1 = y + radius + 1;
            acc -= tmp[static_cast<size_t>(std::clamp(y0, 0, h - 1) * w + x)];
            acc += tmp[static_cast<size_t>(std::clamp(y1, 0, h - 1) * w + x)];
        }
    }
}

} // namespace

Image loadImageRgba(const std::string& path) {
    Image image;
    int n = 0;
    unsigned char* px = stbi_load(path.c_str(), &image.w, &image.h, &n, 4);
    if (!px)
        return {};
    image.rgba.assign(px, px + static_cast<size_t>(image.w) * static_cast<size_t>(image.h) * 4);
    stbi_image_free(px);
    return image;
}

class SoftwareCanvas final : public Canvas {
public:
    SoftwareCanvas(int w, int h) : w_(w), h_(h), px_(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0) {
        const std::string assets = GHOVLZ_ASSETS_DIR;
        // Linux bundles OFL faces. Windows DirectWrite uses Segoe UI and Bahnschrift;
        // these files are the same metrics-ish fallback if this canvas runs there.
        regular_ = loadFont(assets + "/fonts/SourceSans3-Regular.ttf");
        if (!regular_.ok)
            regular_ = loadFont("C:/Windows/Fonts/segoeui.ttf");
        if (!regular_.ok)
            regular_ = loadFont("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
        bold_ = loadFont(assets + "/fonts/SourceSans3-Bold.ttf");
        if (!bold_.ok)
            bold_ = loadFont("C:/Windows/Fonts/segoeuib.ttf");
        if (!bold_.ok)
            bold_ = loadFont("/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf");
        if (!bold_.ok)
            bold_ = regular_;
        wordmark_ = loadFont(assets + "/fonts/Barlow-SemiBold.ttf");
        if (!wordmark_.ok)
            wordmark_ = loadFont("C:/Windows/Fonts/bahnschrift.ttf");
        if (!wordmark_.ok)
            wordmark_ = bold_;
    }

    int width() const override { return w_; }
    int height() const override { return h_; }
    const char* backendName() const override { return "CPU"; }
    void begin() override {}
    void end() override {}

    void clear(Color color) override {
        for (int i = 0; i < w_ * h_; ++i) {
            uint8_t* p = &px_[static_cast<size_t>(i) * 4];
            p[0] = color.r;
            p[1] = color.g;
            p[2] = color.b;
            p[3] = 255;
        }
    }

    void fillRect(Rect r, Color color) override {
        int x0 = std::max(0, static_cast<int>(std::floor(r.x)));
        int y0 = std::max(0, static_cast<int>(std::floor(r.y)));
        int x1 = std::min(w_, static_cast<int>(std::ceil(r.x + r.w)));
        int y1 = std::min(h_, static_cast<int>(std::ceil(r.y + r.h)));
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                if (insideClip(x, y))
                    blend(x, y, color);
    }

    void strokeRect(Rect r, Color color, float thickness) override {
        fillRect({r.x, r.y, r.w, thickness}, color);
        fillRect({r.x, r.y + r.h - thickness, r.w, thickness}, color);
        fillRect({r.x, r.y, thickness, r.h}, color);
        fillRect({r.x + r.w - thickness, r.y, thickness, r.h}, color);
    }

    void fillRoundRect(Rect r, float radius, Color color) override {
        const float rad = std::min(radius, std::min(r.w, r.h) * 0.5f);
        int x0 = std::max(0, static_cast<int>(std::floor(r.x)));
        int y0 = std::max(0, static_cast<int>(std::floor(r.y)));
        int x1 = std::min(w_, static_cast<int>(std::ceil(r.x + r.w)));
        int y1 = std::min(h_, static_cast<int>(std::ceil(r.y + r.h)));
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (!insideClip(x, y) || !inRound(x + 0.5f, y + 0.5f, r, rad))
                    continue;
                blend(x, y, color);
            }
        }
    }

    void fillEllipse(float cx, float cy, float rx, float ry, Color color) override {
        if (rx <= 0.f || ry <= 0.f)
            return;
        int x0 = std::max(0, static_cast<int>(std::floor(cx - rx)));
        int y0 = std::max(0, static_cast<int>(std::floor(cy - ry)));
        int x1 = std::min(w_, static_cast<int>(std::ceil(cx + rx)));
        int y1 = std::min(h_, static_cast<int>(std::ceil(cy + ry)));
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                float nx = (x + 0.5f - cx) / rx;
                float ny = (y + 0.5f - cy) / ry;
                if (nx * nx + ny * ny <= 1.f && insideClip(x, y))
                    blend(x, y, color);
            }
        }
    }

    void line(float x0, float y0, float x1, float y1, Color color, float thickness) override {
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dy)))));
        const float rad = std::max(0.5f, thickness * 0.5f);
        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            fillEllipse(x0 + dx * t, y0 + dy * t, rad, rad, color);
        }
    }

    void arc(float cx, float cy, float radius, float a0, float a1, Color color, float thickness) override {
        const float span = a1 - a0;
        const int steps = std::max(8, static_cast<int>(std::abs(span) * radius * 0.5f));
        float px = cx + std::cos(a0) * radius;
        float py = cy + std::sin(a0) * radius;
        for (int i = 1; i <= steps; ++i) {
            float a = a0 + span * (static_cast<float>(i) / static_cast<float>(steps));
            float x = cx + std::cos(a) * radius;
            float y = cy + std::sin(a) * radius;
            line(px, py, x, y, color, thickness);
            px = x;
            py = y;
        }
    }

    void clip(Rect r) override { clip_ = r; clipped_ = true; }
    void resetClip() override { clipped_ = false; }

    void drawText(const std::string& text, float x, float y, float size, Color color, bool bold) override {
        const FontFile& font = (size >= 22.f && wordmark_.ok) ? wordmark_ : (bold ? bold_ : regular_);
        if (!font.ok) {
            fillRect({x, y, text.size() * size * 0.5f, size}, color.withAlpha(40));
            return;
        }
        const float scale = stbtt_ScaleForPixelHeight(&font.info, size);
        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&font.info, &ascent, &descent, &lineGap);
        float baseline = y + ascent * scale;
        const int fontId = &font == &wordmark_ ? 2 : (&font == &bold_ ? 1 : 0);
        const int pxSize = std::max(1, static_cast<int>(std::lround(size)));
        float pen = x;
        for (unsigned char ch : text) {
            const Glyph& glyph = glyphFor(font, fontId, pxSize, ch, scale);
            if (glyph.gw > 0 && glyph.gh > 0) {
                for (int row = 0; row < glyph.gh; ++row) {
                    for (int col = 0; col < glyph.gw; ++col) {
                        uint8_t cov = glyph.bmp[static_cast<size_t>(row * glyph.gw + col)];
                        if (!cov)
                            continue;
                        int px = static_cast<int>(std::lround(pen + glyph.lsb * scale)) + col + glyph.x0;
                        int py = static_cast<int>(std::lround(baseline)) + row + glyph.y0;
                        Color c = color;
                        c.a = static_cast<uint8_t>(color.a * cov / 255);
                        if (insideClip(px, py))
                            blend(px, py, c);
                    }
                }
            }
            pen += glyph.ax * scale;
        }
    }

    float textWidth(const std::string& text, float size, bool bold) const override {
        const FontFile& font = bold ? bold_ : regular_;
        if (!font.ok)
            return text.size() * size * 0.5f;
        const float scale = stbtt_ScaleForPixelHeight(&font.info, size);
        float w = 0.f;
        for (unsigned char ch : text) {
            int ax = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&font.info, ch, &ax, &lsb);
            w += ax * scale;
        }
        return w;
    }

    void dropShadow(Rect r, float radius, float blur, Color color) override {
        const int pad = blurRadius(blur) * 2 + 2;
        const int bw = std::max(1, static_cast<int>(std::ceil(r.w))) + pad * 2;
        const int bh = std::max(1, static_cast<int>(std::ceil(r.h))) + pad * 2;
        const Key key{bw, bh, blurRadius(blur), static_cast<int>(radius)};
        auto it = shadows_.find(key);
        if (it == shadows_.end()) {
            std::vector<float> a(static_cast<size_t>(bw * bh), 0.f);
            Rect local{static_cast<float>(pad), static_cast<float>(pad), r.w, r.h};
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x)
                    if (inRound(x + 0.5f, y + 0.5f, local, radius))
                        a[static_cast<size_t>(y * bw + x)] = 1.f;
            boxBlur(a, bw, bh, key.blur);
            boxBlur(a, bw, bh, key.blur);
            it = shadows_.emplace(key, std::move(a)).first;
        }
        const int ox = static_cast<int>(std::lround(r.x)) - pad;
        const int oy = static_cast<int>(std::lround(r.y)) - pad + 3;
        const auto& a = it->second;
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                float cover = a[static_cast<size_t>(y * bw + x)];
                if (cover < 0.01f)
                    continue;
                Color c = color;
                c.a = static_cast<uint8_t>(std::clamp(color.a * cover, 0.f, 255.f));
                int dx = ox + x;
                int dy = oy + y;
                if (insideClip(dx, dy))
                    blend(dx, dy, c);
            }
        }
    }

    void blurRect(const std::string& cacheKey, Rect r, float sigma) override {
        int x0 = std::clamp(static_cast<int>(std::floor(r.x)), 0, w_);
        int y0 = std::clamp(static_cast<int>(std::floor(r.y)), 0, h_);
        int x1 = std::clamp(static_cast<int>(std::ceil(r.x + r.w)), 0, w_);
        int y1 = std::clamp(static_cast<int>(std::ceil(r.y + r.h)), 0, h_);
        const int bw = x1 - x0;
        const int bh = y1 - y0;
        if (bw < 2 || bh < 2)
            return;
        auto it = blurs_.find(cacheKey);
        if (it == blurs_.end()) {
            std::vector<uint8_t> copy(static_cast<size_t>(bw * bh * 4));
            for (int y = 0; y < bh; ++y)
                std::copy(px_.begin() + static_cast<size_t>((y0 + y) * w_ + x0) * 4,
                          px_.begin() + static_cast<size_t>((y0 + y) * w_ + x0) * 4 + static_cast<size_t>(bw * 4),
                          copy.begin() + static_cast<size_t>(y * bw * 4));
            for (int channel = 0; channel < 3; ++channel) {
                std::vector<float> plane(static_cast<size_t>(bw * bh));
                for (int i = 0; i < bw * bh; ++i)
                    plane[static_cast<size_t>(i)] = copy[static_cast<size_t>(i) * 4 + channel];
                int rad = blurRadius(sigma);
                boxBlur(plane, bw, bh, rad);
                boxBlur(plane, bw, bh, rad);
                for (int i = 0; i < bw * bh; ++i)
                    copy[static_cast<size_t>(i) * 4 + channel] = static_cast<uint8_t>(std::clamp(plane[static_cast<size_t>(i)], 0.f, 255.f));
            }
            it = blurs_.emplace(cacheKey, std::move(copy)).first;
        }
        const auto& copy = it->second;
        for (int y = 0; y < bh; ++y) {
            for (int x = 0; x < bw; ++x) {
                const uint8_t* s = &copy[static_cast<size_t>(y * bw + x) * 4];
                blend(x0 + x, y0 + y, Color{s[0], s[1], s[2], 255});
            }
        }
    }

    void drawImage(const Image& image, Rect src, Rect dst) override {
        if (image.w < 1 || image.h < 1 || dst.w < 1.f || dst.h < 1.f)
            return;
        for (int y = 0; y < static_cast<int>(dst.h); ++y) {
            for (int x = 0; x < static_cast<int>(dst.w); ++x) {
                float u = src.x + src.w * (x + 0.5f) / dst.w;
                float v = src.y + src.h * (y + 0.5f) / dst.h;
                int ix = std::clamp(static_cast<int>(u), 0, image.w - 1);
                int iy = std::clamp(static_cast<int>(v), 0, image.h - 1);
                const uint8_t* s = &image.rgba[(static_cast<size_t>(iy) * image.w + ix) * 4];
                int dx = static_cast<int>(dst.x) + x;
                int dy = static_cast<int>(dst.y) + y;
                if (insideClip(dx, dy))
                    blend(dx, dy, Color{s[0], s[1], s[2], s[3]});
            }
        }
    }

    bool savePng(const std::string& path) override {
        return stbi_write_png(path.c_str(), w_, h_, 4, px_.data(), w_ * 4) != 0;
    }

    const uint8_t* pixels() const { return px_.data(); }

private:
    struct Key {
        int w, h, blur, rad;
        bool operator<(const Key& o) const {
            if (w != o.w) return w < o.w;
            if (h != o.h) return h < o.h;
            if (blur != o.blur) return blur < o.blur;
            return rad < o.rad;
        }
    };

    bool insideClip(int x, int y) const {
        if (x < 0 || y < 0 || x >= w_ || y >= h_)
            return false;
        if (!clipped_)
            return true;
        return x >= clip_.x && y >= clip_.y && x < clip_.x + clip_.w && y < clip_.y + clip_.h;
    }

    static bool inRound(float x, float y, Rect r, float rad) {
        if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h)
            return false;
        float cx = std::clamp(x, r.x + rad, r.x + r.w - rad);
        float cy = std::clamp(y, r.y + rad, r.y + r.h - rad);
        if (cx == x || cy == y)
            return true;
        float dx = x - cx;
        float dy = y - cy;
        return dx * dx + dy * dy <= rad * rad;
    }

    void blend(int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_)
            return;
        uint8_t* p = &px_[(static_cast<size_t>(y) * w_ + x) * 4];
        const float a = c.a / 255.f;
        p[0] = static_cast<uint8_t>(c.r * a + p[0] * (1.f - a));
        p[1] = static_cast<uint8_t>(c.g * a + p[1] * (1.f - a));
        p[2] = static_cast<uint8_t>(c.b * a + p[2] * (1.f - a));
        p[3] = 255;
    }

    struct Glyph {
        std::vector<unsigned char> bmp;
        int gw = 0, gh = 0, x0 = 0, y0 = 0, ax = 0, lsb = 0;
    };
    struct GKey {
        int font, size, cp;
        bool operator<(const GKey& o) const {
            if (font != o.font) return font < o.font;
            if (size != o.size) return size < o.size;
            return cp < o.cp;
        }
    };
    const Glyph& glyphFor(const FontFile& font, int fontId, int pxSize, int cp, float scale) {
        const GKey key{fontId, pxSize, cp};
        auto it = glyphs_.find(key);
        if (it != glyphs_.end())
            return it->second;
        Glyph g;
        stbtt_GetCodepointHMetrics(&font.info, cp, &g.ax, &g.lsb);
        int x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&font.info, cp, scale, scale, &g.x0, &g.y0, &x1, &y1);
        g.gw = x1 - g.x0;
        g.gh = y1 - g.y0;
        if (g.gw > 0 && g.gh > 0) {
            g.bmp.assign(static_cast<size_t>(g.gw * g.gh), 0);
            stbtt_MakeCodepointBitmap(&font.info, g.bmp.data(), g.gw, g.gh, g.gw, scale, scale, cp);
        }
        return glyphs_.emplace(key, std::move(g)).first->second;
    }

    int w_, h_;
    std::vector<uint8_t> px_;
    FontFile regular_, bold_, wordmark_;
    std::map<GKey, Glyph> glyphs_;
    bool clipped_ = false;
    Rect clip_{};
    std::map<Key, std::vector<float>> shadows_;
    std::map<std::string, std::vector<uint8_t>> blurs_;
};

std::unique_ptr<Canvas> makeSoftwareCanvas(int w, int h) { return std::make_unique<SoftwareCanvas>(w, h); }

const uint8_t* softwarePixels(Canvas& canvas) {
    auto* sw = dynamic_cast<SoftwareCanvas*>(&canvas);
    return sw ? sw->pixels() : nullptr;
}

} // namespace ghovlz
