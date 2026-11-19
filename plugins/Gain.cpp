#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <X11/Xlib.h>
#endif

namespace Steinberg {
namespace Vst {

static constexpr ParamID kGainId = 100;

// HOST-07. Creates a native child (HWND or X11) and paints the gain view into it.
class GainTick final : public Steinberg::Linux::ITimerHandler {
public:
    tresult PLUGIN_API queryInterface(const TUID, void** obj) override {
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }
    uint32 PLUGIN_API release() override { return 1000; }
    void PLUGIN_API onTimer() override {}
};

class GainView final : public IPlugView, public IPlugViewContentScaleSupport {
public:
    GainView() { FUNKNOWN_CTOR }
    ~GainView() {
        unhookRunLoop();
        destroyNative();
        FUNKNOWN_DTOR
    }

    DECLARE_FUNKNOWN_METHODS

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE {
        if (!type)
            return kResultFalse;
        if (std::strcmp(type, kPlatformTypeHWND) == 0 || std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0)
            return kResultTrue;
        return kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) SMTG_OVERRIDE {
        if (!parent || isPlatformTypeSupported(type) != kResultTrue)
            return kResultFalse;
        destroyNative();
        parent_ = parent;
        return createNative() ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API removed() SMTG_OVERRIDE {
        unhookRunLoop();
        destroyNative();
        parent_ = nullptr;
        frame_ = nullptr;
        return kResultOk;
    }
    tresult PLUGIN_API onWheel(float) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect* size) SMTG_OVERRIDE {
        if (!size)
            return kInvalidArgument;
        *size = rect_;
        return kResultOk;
    }
    tresult PLUGIN_API onSize(ViewRect* newSize) SMTG_OVERRIDE {
        if (!newSize || newSize->getWidth() < 1 || newSize->getHeight() < 1)
            return kInvalidArgument;
        rect_ = *newSize;
        if (scale_ > 0.f) {
            baseW_ = std::max(1, static_cast<int>(std::lround(static_cast<float>(rect_.getWidth()) / scale_)));
            baseH_ = std::max(1, static_cast<int>(std::lround(static_cast<float>(rect_.getHeight()) / scale_)));
        }
        resizeNative();
        paintNative();
        return kResultOk;
    }
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) SMTG_OVERRIDE {
        if (!(factor >= 0.5f && factor <= 4.f))
            return kInvalidArgument;
        scale_ = factor;
        ViewRect next {0, 0, std::max(1, static_cast<int>(std::lround(static_cast<float>(baseW_) * scale_))),
                       std::max(1, static_cast<int>(std::lround(static_cast<float>(baseH_) * scale_)))};
        if (frame_)
            return frame_->resizeView(this, &next);
        rect_ = next;
        return kResultOk;
    }
    tresult PLUGIN_API onFocus(TBool) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame* frame) SMTG_OVERRIDE {
        frame_ = frame;
        return kResultOk;
    }
    tresult PLUGIN_API canResize() SMTG_OVERRIDE { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) SMTG_OVERRIDE {
        if (!rect)
            return kInvalidArgument;
        if (rect->getWidth() < 160)
            rect->right = rect->left + 160;
        if (rect->getHeight() < 120)
            rect->bottom = rect->top + 120;
        return kResultOk;
    }

private:
    bool createNative() {
        const int w = rect_.getWidth();
        const int h = rect_.getHeight();
#if defined(_WIN32)
        static bool registered = false;
        if (!registered) {
            WNDCLASSW cls {};
            cls.lpfnWndProc = DefWindowProcW;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = L"GhovlzGainView";
            cls.hbrBackground = CreateSolidBrush(RGB(10, 10, 10));
            registered = RegisterClassW(&cls) != 0;
        }
        hwnd_ = CreateWindowExW(0, L"GhovlzGainView", L"", WS_CHILD | WS_VISIBLE, 0, 0, w, h,
                                static_cast<HWND>(parent_), nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd_)
            return false;
        paintNative();
        hookRunLoop();
        return true;
#else
        display_ = XOpenDisplay(nullptr);
        if (!display_)
            return false;
        const Window parent = static_cast<Window>(reinterpret_cast<uintptr_t>(parent_));
        XSetWindowAttributes attr {};
        attr.background_pixel = 0x000A0A0A;
        attr.backing_store = Always;
        child_ = XCreateWindow(display_, parent, 0, 0, static_cast<unsigned>(w), static_cast<unsigned>(h), 0, CopyFromParent,
                               InputOutput, CopyFromParent, CWBackPixel | CWBackingStore, &attr);
        if (!child_) {
            XCloseDisplay(display_);
            display_ = nullptr;
            return false;
        }
        gc_ = XCreateGC(display_, child_, 0, nullptr);
        XMapWindow(display_, child_);
        paintNative();
        XFlush(display_);
        hookRunLoop();
        return true;
#endif
    }
    void hookRunLoop() {
        if (!frame_ || timerOn_)
            return;
        Linux::IRunLoop* loop = nullptr;
        if (frame_->queryInterface(Linux::IRunLoop::iid, reinterpret_cast<void**>(&loop)) != kResultOk || !loop)
            return;
        if (loop->registerTimer(&tick_, 32) == kResultOk)
            timerOn_ = true;
        loop->release();
    }
    void unhookRunLoop() {
        if (!frame_ || !timerOn_)
            return;
        Linux::IRunLoop* loop = nullptr;
        if (frame_->queryInterface(Linux::IRunLoop::iid, reinterpret_cast<void**>(&loop)) == kResultOk && loop) {
            loop->unregisterTimer(&tick_);
            loop->release();
        }
        timerOn_ = false;
    }
    void resizeNative() {
        const int w = rect_.getWidth();
        const int h = rect_.getHeight();
#if defined(_WIN32)
        if (hwnd_)
            MoveWindow(hwnd_, 0, 0, w, h, TRUE);
#else
        if (display_ && child_) {
            XResizeWindow(display_, child_, static_cast<unsigned>(w), static_cast<unsigned>(h));
            XSync(display_, False);
        }
#endif
    }
    void paintNative() {
        const int w = std::max(1, rect_.getWidth());
        const int h = std::max(1, rect_.getHeight());
#if defined(_WIN32)
        if (!hwnd_)
            return;
        HDC hdc = GetDC(hwnd_);
        if (!hdc)
            return;
        RECT rc {0, 0, w, h};
        HBRUSH bg = CreateSolidBrush(RGB(10, 10, 10));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        HBRUSH gold = CreateSolidBrush(RGB(180, 150, 55));
        SelectObject(hdc, gold);
        const int cx = w / 2;
        const int cy = h / 2;
        const int disc = std::max(24, static_cast<int>(std::lround(36.f * scale_)));
        const int bar = std::max(8, static_cast<int>(std::lround(8.f * scale_)));
        RECT barRc {0, 0, w, bar};
        FillRect(hdc, &barRc, gold);
        Ellipse(hdc, cx - disc, cy - disc, cx + disc, cy + disc);
        DeleteObject(gold);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(245, 245, 245));
        TextOutW(hdc, 16, 16, L"GHOVLZ Gain", 11);
        ReleaseDC(hwnd_, hdc);
#else
        if (!display_ || !child_ || !gc_)
            return;
        for (int attempt = 0; attempt < 40; ++attempt) {
            XWindowAttributes attrs {};
            if (XGetWindowAttributes(display_, child_, &attrs) && attrs.map_state == IsViewable)
                break;
            XSync(display_, False);
        }
        const int bar = std::max(8, static_cast<int>(std::lround(8.f * scale_)));
        const int disc = std::max(24, static_cast<int>(std::lround(36.f * scale_)));
        XSetForeground(display_, gc_, 0x000A0A0A);
        XFillRectangle(display_, child_, gc_, 0, 0, static_cast<unsigned>(w), static_cast<unsigned>(h));
        XSetForeground(display_, gc_, 0x00B49637);
        XFillRectangle(display_, child_, gc_, 0, 0, static_cast<unsigned>(w), static_cast<unsigned>(bar));
        XFillArc(display_, child_, gc_, w / 2 - disc, h / 2 - disc, disc * 2, disc * 2, 0, 360 * 64);
        XSetForeground(display_, gc_, 0x00F5F5F5);
        XDrawString(display_, child_, gc_, 16, 28, "GHOVLZ Gain", 11);
        XSync(display_, False);
#endif
    }
    void destroyNative() {
#if defined(_WIN32)
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
#else
        if (display_) {
            if (gc_)
                XFreeGC(display_, gc_);
            if (child_)
                XDestroyWindow(display_, child_);
            XCloseDisplay(display_);
        }
        display_ = nullptr;
        child_ = 0;
        gc_ = nullptr;
#endif
    }

    void* parent_ = nullptr;
    IPlugFrame* frame_ = nullptr;
    ViewRect rect_ {0, 0, 480, 300};
    int baseW_ = 480;
    int baseH_ = 300;
    float scale_ = 1.f;
    GainTick tick_;
    bool timerOn_ = false;
#if defined(_WIN32)
    HWND hwnd_ = nullptr;
#else
    Display* display_ = nullptr;
    Window child_ = 0;
    GC gc_ = nullptr;
#endif
};

tresult PLUGIN_API GainView::queryInterface(const TUID iid, void** obj) {
    QUERY_INTERFACE(iid, obj, IPlugViewContentScaleSupport::iid, IPlugViewContentScaleSupport)
    QUERY_INTERFACE(iid, obj, IPlugView::iid, IPlugView)
    QUERY_INTERFACE(iid, obj, FUnknown::iid, IPlugView)
    *obj = nullptr;
    return kNoInterface;
}
IMPLEMENT_REFCOUNT(GainView)

class GainProcessor final : public SingleComponentEffect {
public:
    static FUnknown* createInstance(void*) { return static_cast<IAudioProcessor*>(new GainProcessor); }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE {
        const tresult r = SingleComponentEffect::initialize(context);
        if (r != kResultOk)
            return r;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        parameters.addParameter(STR16("Gain"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, kGainId);
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) SMTG_OVERRIDE {
        const tresult r = SingleComponentEffect::setParamNormalized(id, value);
        if (id == kGainId)
            gain_ = static_cast<float>(value) * 2.f;
        return r;
    }
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE {
        if (data.inputParameterChanges) {
            const int32 queues = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < queues; ++i) {
                auto* queue = data.inputParameterChanges->getParameterData(i);
                if (!queue || queue->getParameterId() != kGainId || queue->getPointCount() < 1)
                    continue;
                int32 offset = 0;
                ParamValue value = 0;
                queue->getPoint(queue->getPointCount() - 1, offset, value);
                gain_ = static_cast<float>(value) * 2.f;
                setParamNormalized(kGainId, value);
            }
        }
        if (data.numSamples <= 0 || !data.inputs || !data.outputs)
            return kResultOk;
        const int32 chs = data.inputs[0].numChannels;
        for (int32 c = 0; c < chs && c < data.outputs[0].numChannels; ++c) {
            float* in = data.inputs[0].channelBuffers32 ? data.inputs[0].channelBuffers32[c] : nullptr;
            float* out = data.outputs[0].channelBuffers32 ? data.outputs[0].channelBuffers32[c] : nullptr;
            if (!in || !out)
                continue;
            for (int32 i = 0; i < data.numSamples; ++i)
                out[i] = in[i] * gain_;
        }
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* stream) SMTG_OVERRIDE {
        if (!stream)
            return kInvalidArgument;
        int32 written = 0;
        stream->write(&gain_, static_cast<int32>(sizeof(gain_)), &written);
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* stream) SMTG_OVERRIDE {
        if (!stream)
            return kInvalidArgument;
        int32 read = 0;
        float g = 1.f;
        stream->read(&g, static_cast<int32>(sizeof(g)), &read);
        gain_ = g;
        if (auto* p = parameters.getParameter(kGainId))
            p->setNormalized(g * 0.5);
        return kResultOk;
    }
    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE {
        if (name && std::strcmp(name, ViewType::kEditor) == 0)
            return new GainView;
        return nullptr;
    }

private:
    float gain_ = 1.f;
};

} // namespace Vst
} // namespace Steinberg

BEGIN_FACTORY_DEF("Decksounds", "https://decksounds.com", "mailto:dev@decksounds.com")
DEF_CLASS2(INLINE_UID(0xA1B2C3D4, 0x11112222, 0x33334444, 0x55556666),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           "GHOVLZ Test Gain",
           0,
           "Fx",
           "1.0.0",
           kVstVersionString,
           Steinberg::Vst::GainProcessor::createInstance)
END_FACTORY
