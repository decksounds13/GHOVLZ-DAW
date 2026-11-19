#include "app/Platform.h"

#include "audio/AppSettings.h"
#include "audio/AudioIO.h"
#include "midi/MidiIO.h"
#include "ui/Canvas.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace ghovlz {
namespace {

void blit(Display* dpy, Window win, GC gc, const uint8_t* rgba, int w, int h) {
    Visual* vis = DefaultVisual(dpy, DefaultScreen(dpy));
    const int depth = DefaultDepth(dpy, DefaultScreen(dpy));
    auto shift = [](unsigned long mask) {
        int s = 0;
        while (mask && (mask & 1) == 0) {
            mask >>= 1;
            ++s;
        }
        return s;
    };
    const int rs = shift(vis->red_mask);
    const int gs = shift(vis->green_mask);
    const int bs = shift(vis->blue_mask);
    std::vector<uint32_t> pix(static_cast<size_t>(w * h));
    for (int i = 0; i < w * h; ++i) {
        pix[static_cast<size_t>(i)] = (static_cast<uint32_t>(rgba[i * 4]) << rs) |
                                      (static_cast<uint32_t>(rgba[i * 4 + 1]) << gs) |
                                      (static_cast<uint32_t>(rgba[i * 4 + 2]) << bs);
    }
    XImage* img = XCreateImage(dpy, vis, static_cast<unsigned>(depth), ZPixmap, 0,
                               reinterpret_cast<char*>(pix.data()), static_cast<unsigned>(w),
                               static_cast<unsigned>(h), 32, 0);
    if (!img)
        return;
    XPutImage(dpy, win, gc, img, 0, 0, 0, 0, static_cast<unsigned>(w), static_cast<unsigned>(h));
    img->data = nullptr;
    XDestroyImage(img);
}

} // namespace

int runWindow(AppHost& host) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "GHOVLZ DAW: no X display\n";
        return 1;
    }
    const int w = 1680;
    const int h = 980;
    const int screen = DefaultScreen(dpy);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, w, h, 0, 0, 0x0A0A0A);
    XStoreName(dpy, win, "GHOVLZ! DAW");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
    XMapWindow(dpy, win);
    GC gc = XCreateGC(dpy, win, 0, nullptr);

    AudioIO audio;
    const AppSettings saved = AppSettings::load(AppSettings::userPath());
    const OpenFallback opened = openOrFallback(saved, [&](const AudioSettings& settings) { return audio.open(host.engine, settings); });
    host.frame.audioBackend = audio.backendName();
    host.frame.prefBackend = opened.used.backend;
    host.frame.prefDevice = opened.used.device.empty() ? "Default" : opened.used.device;
    host.frame.prefRate = opened.used.sampleRate;
    host.frame.prefFrames = opened.used.frames;
    host.frame.prefMessage = opened.message;
    int reopenAttempt = 0;
    MidiIO midi;
    midi.open();
    host.frame.midiPort = midi.portName();
    host.frame.backend = "CPU";

    auto canvas = makeSoftwareCanvas(w, h);
    bool quit = false;
    while (!quit) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == ButtonPress || ev.type == ButtonRelease || ev.type == MotionNotify) {
                PointerEvent pointer;
                pointer.phase = ev.type == ButtonPress ? PointerEvent::Phase::Down
                                : ev.type == ButtonRelease ? PointerEvent::Phase::Up
                                                           : PointerEvent::Phase::Move;
                pointer.x = static_cast<float>(ev.type == MotionNotify ? ev.xmotion.x : ev.xbutton.x);
                pointer.y = static_cast<float>(ev.type == MotionNotify ? ev.xmotion.y : ev.xbutton.y);
                const unsigned state = ev.type == MotionNotify ? ev.xmotion.state : ev.xbutton.state;
                pointer.fine = (state & ShiftMask) != 0;
                if (uiPointer(host.frame, host.project.get(), host.engine, host.project, host.themes, pointer))
                    continue;
                if (ev.type != ButtonPress)
                    continue;
                const Hit hit = hitTest(pointer.x, pointer.y, w, h, 1.f, host.frame.view);
                if (hit == Hit::Play) {
                    if (host.engine->playing())
                        host.engine->stop();
                    else
                        host.engine->play();
                } else if (hit == Hit::Stop) {
                    host.engine->stop();
                    host.engine->seek(0);
                } else if (hit == Hit::Rec) {
                    host.engine->setRecording(!host.engine->recording());
                } else if (hit == Hit::Loop && host.project) {
                    host.project->loop = !host.project->loop;
                    host.engine->setProject(host.project);
                } else if (hit == Hit::Metro && host.project) {
                    host.project->metronome = !host.project->metronome;
                    host.engine->setProject(host.project);
                } else {
                    host.frame.view = viewFromHit(hit, host.frame.view);
                }
            } else if (ev.type == KeyPress) {
                char typed[8] = {};
                KeySym sym = 0;
                const int typedCount = XLookupString(&ev.xkey, typed, sizeof(typed), &sym, nullptr);
                if (sym == XK_BackSpace || sym == XK_Delete) {
                    if (uiKey(host.frame, host.project.get(), host.engine, host.project, host.themes, KeyEvent{0, true}))
                        continue;
                } else if (typedCount == 1 && typed[0] >= 32) {
                    if (uiKey(host.frame, host.project.get(), host.engine, host.project, host.themes,
                              KeyEvent{static_cast<uint32_t>(static_cast<unsigned char>(typed[0])), false}))
                        continue;
                }
                if (sym == XK_Escape || sym == XK_q)
                    quit = true;
                else if (sym == XK_space) {
                    if (host.engine->playing())
                        host.engine->stop();
                    else
                        host.engine->play();
                }                 else if (sym >= XK_1 && sym <= XK_8) {
                    host.frame.view = static_cast<View>(sym - XK_1);
                } else if ((sym == XK_z || sym == XK_Z) && (ev.xkey.state & ControlMask) && host.project) {
                    if (ev.xkey.state & ShiftMask)
                        host.undo.redo(*host.project);
                    else
                        host.undo.undo(*host.project);
                    host.engine->setProject(host.project);
                } else if ((sym == XK_y || sym == XK_Y) && (ev.xkey.state & ControlMask) && host.project) {
                    host.undo.redo(*host.project);
                    host.engine->setProject(host.project);
                }
            } else if (ev.type == DestroyNotify) {
                quit = true;
            }
        }
        if (audio.wantsReopen()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(recoverBackoffMs(reopenAttempt)));
            if (audio.recover())
                reopenAttempt = 0;
            else
                ++reopenAttempt;
            host.frame.audioBackend = audio.backendName();
        }
        midi.poll();
        if (host.engine)
            host.engine->pumpEditors();
        host.frame.playing = host.engine->playing();
        host.frame.recording = host.engine->recording();
        host.frame.playhead = host.engine->playhead();
        host.frame.cpu = host.engine->cpuLoad();
        paintFrame(*canvas, host.frame);
        const uint8_t* px = softwarePixels(*canvas);
        if (px)
            blit(dpy, win, gc, px, w, h);
        XFlush(dpy);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    audio.close();
    midi.close();
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}

} // namespace ghovlz
