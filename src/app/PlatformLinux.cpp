#include "app/Platform.h"

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
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(dpy, win);
    GC gc = XCreateGC(dpy, win, 0, nullptr);

    AudioIO audio;
    if (audio.open(host.engine, 48000))
        host.frame.audioBackend = audio.backendName();
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
            if (ev.type == ButtonPress) {
                const Hit hit = hitTest(static_cast<float>(ev.xbutton.x), static_cast<float>(ev.xbutton.y), w, h);
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
                } else {
                    host.frame.view = viewFromHit(hit, host.frame.view);
                }
            } else if (ev.type == KeyPress) {
                const KeySym sym = XLookupKeysym(&ev.xkey, 0);
                if (sym == XK_Escape || sym == XK_q)
                    quit = true;
                else if (sym == XK_space) {
                    if (host.engine->playing())
                        host.engine->stop();
                    else
                        host.engine->play();
                } else if (sym >= XK_1 && sym <= XK_8) {
                    host.frame.view = static_cast<View>(sym - XK_1);
                }
            } else if (ev.type == DestroyNotify) {
                quit = true;
            }
        }
        midi.poll();
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
