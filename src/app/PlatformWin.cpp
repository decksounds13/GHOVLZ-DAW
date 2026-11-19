#include "app/Platform.h"

#include "audio/AudioIO.h"
#include "midi/MidiIO.h"
#include "ui/Canvas.h"
#include "ui/Direct2DCanvas.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace ghovlz {
namespace {

struct WinState {
    AppHost* host = nullptr;
    std::unique_ptr<Canvas> canvas;
    bool direct2d = false;
    AudioIO audio;
    MidiIO midi;
    int width = 1680;
    int height = 980;
    bool quit = false;
};

void paint(WinState& state) {
    if (!state.canvas || !state.host)
        return;
    state.host->frame.playing = state.host->engine->playing();
    state.host->frame.recording = state.host->engine->recording();
    state.host->frame.playhead = state.host->engine->playhead();
    state.host->frame.cpu = state.host->engine->cpuLoad();
    state.host->frame.backend = state.canvas->backendName();
    state.host->frame.audioBackend = state.audio.backendName();
    state.host->frame.midiPort = state.midi.portName();
    paintFrame(*state.canvas, state.host->frame);
    if (!state.direct2d) {
        const uint8_t* rgba = softwarePixels(*state.canvas);
        if (!rgba)
            return;
        std::vector<uint8_t> bgra(static_cast<size_t>(state.width * state.height * 4));
        for (int i = 0; i < state.width * state.height; ++i) {
            bgra[static_cast<size_t>(i) * 4] = rgba[i * 4 + 2];
            bgra[static_cast<size_t>(i) * 4 + 1] = rgba[i * 4 + 1];
            bgra[static_cast<size_t>(i) * 4 + 2] = rgba[i * 4];
            bgra[static_cast<size_t>(i) * 4 + 3] = 255;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = state.width;
        info.bmiHeader.biHeight = -state.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HDC dc = GetDC(GetActiveWindow());
        StretchDIBits(dc, 0, 0, state.width, state.height, 0, 0, state.width, state.height, bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(GetActiveWindow(), dc);
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<WinState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_LBUTTONDOWN:
            if (state && state->host) {
                const Hit hit = hitTest(static_cast<float>(LOWORD(lp)), static_cast<float>(HIWORD(lp)), state->width, state->height);
                if (hit == Hit::Play) {
                    if (state->host->engine->playing())
                        state->host->engine->stop();
                    else
                        state->host->engine->play();
                } else if (hit == Hit::Stop) {
                    state->host->engine->stop();
                    state->host->engine->seek(0);
                } else if (hit == Hit::Rec) {
                    state->host->engine->setRecording(!state->host->engine->recording());
                } else {
                    state->host->frame.view = viewFromHit(hit, state->host->frame.view);
                }
            }
            return 0;
        case WM_KEYDOWN:
            if (state && state->host) {
                if (wp == VK_ESCAPE)
                    state->quit = true;
                else if (wp == VK_SPACE) {
                    if (state->host->engine->playing())
                        state->host->engine->stop();
                    else
                        state->host->engine->play();
                } else if (wp >= '1' && wp <= '8') {
                    state->host->frame.view = static_cast<View>(wp - '1');
                }
            }
            return 0;
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SIZE:
            if (state && state->direct2d && state->canvas && wp != SIZE_MINIMIZED) {
                state->width = LOWORD(lp);
                state->height = HIWORD(lp);
                resizeDirect2DCanvas(*state->canvas, state->width, state->height);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int runWindow(AppHost& host) {
    if (auto setDpi = reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))
        setDpi(reinterpret_cast<HANDLE>(-4)); // per-monitor v2

    WinState state;
    state.host = &host;
    const wchar_t* cls = L"GhovlzDawWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT rc{0, 0, state.width, state.height};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(cls, L"GHOVLZ! DAW", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    ShowWindow(hwnd, SW_SHOW);

    state.canvas = makeDirect2DCanvas(hwnd, state.width, state.height);
    state.direct2d = static_cast<bool>(state.canvas);
    if (!state.canvas)
        state.canvas = makeSoftwareCanvas(state.width, state.height);
    if (state.audio.open(host.engine, 48000))
        host.frame.audioBackend = state.audio.backendName();
    state.midi.open();

    MSG msg{};
    while (!state.quit) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                state.quit = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        state.midi.poll();
        paint(state);
        Sleep(16);
    }
    state.audio.close();
    state.midi.close();
    state.canvas.reset();
    DestroyWindow(hwnd);
    return 0;
}

} // namespace ghovlz
