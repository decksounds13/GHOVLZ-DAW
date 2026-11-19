#include "engine/Engine.h"
#include "core/RtGuard.h"
#include "dsp/Devices.h"
#include "host/PluginHost.h"
#include "io/WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace ghovlz {
namespace {

struct LiveSend {
    std::string target;
    std::atomic<float> db {-100.f};
    std::atomic<bool> pre {false};
};

struct Node {
    std::string id;
    TrackType type = TrackType::Audio;
    std::string output;
    std::atomic<float> volumeDb {0.f};
    std::atomic<float> pan {0.f};
    std::atomic<bool> mute {false};
    std::atomic<bool> solo {false};
    std::atomic<bool> arm {false};
    std::vector<std::unique_ptr<LiveSend>> sends;
    std::vector<std::shared_ptr<Device>> devices;
    std::vector<Clip> clips;
    std::vector<std::optional<Clip>> slots;
    std::atomic<int> sessionScene {-1};
    std::atomic<double> sessionOrigin {0};
    std::atomic<float> meter {0.f};
    std::atomic<float> hold {0.f};
    std::atomic<uint8_t> clipLamp {0};
    int chainLatency = 0;
    AudioBuffer work;
    AudioBuffer shaped;
    MidiBuffer midiIn;
    Engine::DelayLineState* delay = nullptr;
    std::atomic<Engine::RecSlot*> rec {nullptr};
};

struct AutoApply {
    Node* node = nullptr;
    int kind = 0;
    std::vector<AutoPoint> points;
};

struct EngineGraph {
    double bpm = 120;
    int numerator = 4;
    int denominator = 4;
    bool loop = false;
    double loopStart = 0;
    double loopEnd = 16;
    bool metronome = false;
    float metronomeLevel = 0.3f;
    std::vector<Lane> automation;
    std::vector<AutoApply> autos;
    std::map<std::string, MediaPtr> media;
    std::vector<std::unique_ptr<Node>> nodes;

    Node* find(const std::string& id) {
        for (auto& n : nodes)
            if (n->id == id)
                return n.get();
        return nullptr;
    }
};

void rememberPeak(Node* n, float pk) {
    n->meter.store(std::max(pk, n->meter.load() * 0.92f));
    n->hold.store(std::max(pk, n->hold.load() * 0.998f));
    if (pk >= 0.98f)
        n->clipLamp.store(1);
}
void fitBuffer(AudioBuffer& b, int frames) {
    if (b.channels != 2 || b.frames != frames || b.data.size() < static_cast<size_t>(frames) * 2)
        b.resize(2, frames);
    else
        b.clear();
}

void delayWriteAdd(Engine::DelayLineState& line, const AudioBuffer& src, AudioBuffer& dst, int delaySamples) {
    delaySamples = std::max(0, delaySamples);
    if (line.cap <= delaySamples) {
        line.cap = std::max(8, delaySamples + 8);
        for (auto& ch : line.data)
            ch.assign(line.cap, 0.f);
        line.w = 0;
    }
    if (line.delay != delaySamples) {
        line.delay = delaySamples;
        for (auto& ch : line.data)
            std::fill(ch.begin(), ch.end(), 0.f);
        line.w = 0;
    }
    const int n = std::min(src.frames, dst.frames);
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < 2; ++c) {
            const float in = c < src.channels ? src.ch(c)[i] : 0.f;
            float y = in;
            if (delaySamples > 0)
                y = line.data[c][(line.w - delaySamples + line.cap * 4) % line.cap];
            line.data[c][line.w] = in;
            if (c < dst.channels)
                dst.ch(c)[i] += y;
        }
        line.w = (line.w + 1) % line.cap;
    }
}

float evalPoints(const std::vector<AutoPoint>& points, double q) {
    if (points.empty())
        return 0.f;
    if (q <= points.front().quarter)
        return points.front().value;
    for (size_t i = 1; i < points.size(); ++i) {
        if (q <= points[i].quarter) {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            const double span = std::max(1e-9, b.quarter - a.quarter);
            const float t = static_cast<float>((q - a.quarter) / span);
            return a.value + (b.value - a.value) * t;
        }
    }
    return points.back().value;
}

void panGains(float pan, float& l, float& r) {
    const float angle = (pan + 1.f) * 0.78539816f;
    l = std::cos(angle);
    r = std::sin(angle);
}

} // namespace

struct Engine::Graph : EngineGraph {};

Engine::Engine() = default;
Engine::~Engine() {
    published_.store(nullptr, std::memory_order_release);
    while (readers_.load(std::memory_order_acquire) != 0) {
    }
    owned_.clear();
}

Engine::Graph* Engine::acquire() const {
    readers_.fetch_add(1, std::memory_order_acq_rel);
    return published_.load(std::memory_order_acquire);
}
void Engine::releaseGraph() const { readers_.fetch_sub(1, std::memory_order_release); }

void Engine::setPluginHost(std::shared_ptr<PluginHost> host) { plugins_ = std::move(host); }

void Engine::prepare(double sampleRate, int block) {
    sampleRate_ = sampleRate;
    block_ = block;
#if defined(__SSE__)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    Graph* g = acquire();
    if (g) {
        for (auto& n : g->nodes)
            for (auto& d : n->devices)
                d->prepare(sampleRate_, block_);
    }
    releaseGraph();
    fitBuffer(mix_, block_);
}

void Engine::release() {}

std::shared_ptr<Device> Engine::makeLive(const DeviceState& state) {
    std::shared_ptr<Device> live;
    if (state.type == "vst3" && plugins_) {
        std::string err;
        live = plugins_->instantiate(state.pluginPath, err);
    }
    if (!live)
        live = createDevice(state.type == "vst3" ? "missing" : state.type);
    live->id = state.id;
    if (!state.name.empty())
        live->name = state.name;
    live->type = state.type == "vst3" && live->type != "missing" ? "vst3" : live->type;
    live->bypass.store(state.bypass);
    for (const auto& kv : state.params)
        live->set(kv.first, kv.second);
    if (!state.state.empty() && plugins_)
        plugins_->setState(*live, state.state);
    live->prepare(sampleRate_, block_);
    return live;
}

void Engine::publish() {
    if (!project_) {
        Graph* old = published_.exchange(nullptr, std::memory_order_acq_rel);
        while (readers_.load(std::memory_order_acquire) != 0) {
        }
        if (old)
            owned_.clear();
        return;
    }
    Graph* previous = published_.load(std::memory_order_acquire);
    auto g = std::make_unique<Graph>();
    g->bpm = project_->bpm;
    g->numerator = project_->numerator;
    g->denominator = project_->denominator;
    g->loop = project_->loop;
    g->loopStart = project_->loopStart;
    g->loopEnd = project_->loopEnd;
    g->metronome = project_->metronome;
    g->metronomeLevel = project_->metronomeLevel;
    g->automation = project_->automation;
    g->media = project_->media;
    playhead_.store(project_->playhead);

    auto addNode = [&](const Track& t) {
        auto n = std::make_unique<Node>();
        n->id = t.id;
        n->type = t.type;
        n->output = t.output.empty() && t.type != TrackType::Master ? "master" : t.output;
        n->volumeDb.store(t.volumeDb);
        n->pan.store(t.pan);
        n->mute.store(t.mute);
        n->solo.store(t.solo);
        n->arm.store(t.arm);
        n->clips = t.clips;
        n->slots = t.slots;
        for (const auto& s : t.sends) {
            auto ls = std::make_unique<LiveSend>();
            ls->target = s.target;
            ls->db.store(s.levelDb);
            ls->pre.store(s.pre);
            n->sends.push_back(std::move(ls));
        }
        int lat = 0;
        for (const auto& d : t.devices) {
            auto live = makeLive(d);
            lat += live->latency();
            n->devices.push_back(std::move(live));
        }
        n->chainLatency = lat;
        if (previous) {
            if (auto* old = previous->find(t.id)) {
                n->meter.store(old->meter.load());
                n->hold.store(old->hold.load());
                n->clipLamp.store(old->clipLamp.load());
                n->sessionScene.store(old->sessionScene.load());
                n->sessionOrigin.store(old->sessionOrigin.load());
                n->rec.store(old->rec.load());
            }
        }
        n->work.resize(2, std::max(1, block_));
        n->shaped.resize(2, std::max(1, block_));
        n->midiIn.events.reserve(512);
        g->nodes.push_back(std::move(n));
    };
    for (const auto& t : project_->tracks)
        addNode(t);
    addNode(project_->master);
    const int cap = std::max(64, static_cast<int>(sampleRate_) * 2);
    for (auto& n : g->nodes) {
        const bool leaf = n->type == TrackType::Audio || n->type == TrackType::Midi || n->type == TrackType::Return;
        if (!leaf)
            continue;
        auto& line = delays_[n->id];
        if (line.cap < cap) {
            line.cap = cap;
            line.w = 0;
            line.delay = 0;
            line.data[0].assign(static_cast<size_t>(cap), 0.f);
            line.data[1].assign(static_cast<size_t>(cap), 0.f);
        }
        n->delay = &line;
    }
    for (const auto& lane : g->automation) {
        const std::string prefix = "track:";
        if (lane.id.rfind(prefix, 0) != 0)
            continue;
        const auto rest = lane.id.substr(prefix.size());
        const auto colon = rest.rfind(':');
        if (colon == std::string::npos)
            continue;
        const auto tid = rest.substr(0, colon);
        const auto what = rest.substr(colon + 1);
        AutoApply apply;
        apply.node = g->find(tid);
        apply.points = lane.points;
        if (!apply.node)
            continue;
        if (what == "gain")
            apply.kind = 0;
        else if (what == "pan")
            apply.kind = 1;
        else
            continue;
        g->autos.push_back(std::move(apply));
    }
    Graph* raw = g.get();
    owned_.push_back(std::move(g));
    Graph* old = published_.exchange(raw, std::memory_order_acq_rel);
    while (readers_.load(std::memory_order_acquire) != 0) {
    }
    if (old) {
        owned_.erase(std::remove_if(owned_.begin(), owned_.end(), [&](const std::unique_ptr<Graph>& p) { return p.get() == old; }),
                     owned_.end());
    }
}

void Engine::setProject(std::shared_ptr<Project> project) {
    project_ = std::move(project);
    publish();
}
std::shared_ptr<Project> Engine::project() const { return project_; }

static int routeOf(EngineGraph& g, Node* n, int guard = 0) {
    if (!n || guard > 16)
        return 0;
    int r = n->chainLatency;
    if (n->type != TrackType::Master && !n->output.empty() && n->output != "master") {
        if (auto* p = g.find(n->output))
            if (p->type == TrackType::Group)
                r += routeOf(g, p, guard + 1);
    }
    return r;
}

static bool ancestorMuted(EngineGraph& g, Node* n) {
    Node* p = n;
    for (int i = 0; i < 16 && p && p->type != TrackType::Master && !p->output.empty() && p->output != "master"; ++i) {
        p = g.find(p->output);
        if (p && p->mute.load())
            return true;
    }
    return false;
}
static bool ancestorSolo(EngineGraph& g, Node* n) {
    Node* p = n;
    for (int i = 0; i < 16 && p && p->type != TrackType::Master && !p->output.empty() && p->output != "master"; ++i) {
        p = g.find(p->output);
        if (p && p->solo.load())
            return true;
    }
    return false;
}
static bool descendantSolo(EngineGraph& g, Node* n) {
    for (auto& c : g.nodes) {
        if (c->output == n->id && (c->solo.load() || descendantSolo(g, c.get())))
            return true;
    }
    return false;
}

void Engine::process(float* interleavedOut, const float* interleavedIn, int frames) {
    RtScope audioThread;
    Graph* g = acquire();
    if (!g || frames <= 0) {
        releaseGraph();
        if (interleavedOut && frames > 0)
            std::fill(interleavedOut, interleavedOut + frames * 2, 0.f);
        return;
    }
#if defined(__SSE__)
    thread_local int ftz = 0;
    if (!ftz) {
        _mm_setcsr(_mm_getcsr() | 0x8040);
        ftz = 1;
    }
#endif
    const double spq = sampleRate_ * 60.0 / std::max(1.0, g->bpm);
    bool anySolo = false;
    int maxRoute = 0;
    for (auto& n : g->nodes) {
        if (n->solo.load())
            anySolo = true;
        if (n->type == TrackType::Audio || n->type == TrackType::Midi || n->type == TrackType::Return)
            maxRoute = std::max(maxRoute, routeOf(*g, n.get()));
    }

    fitBuffer(mix_, frames);
    for (auto& n : g->nodes)
        if (n->type == TrackType::Return)
            fitBuffer(n->work, frames);

    auto render = [&](auto& self, Node* n) -> void {
        fitBuffer(n->work, frames);
        AudioBuffer& buf = n->work;
        const bool group = n->type == TrackType::Group;
        if (group) {
            for (auto& c : g->nodes) {
                if (c->output != n->id)
                    continue;
                self(self, c.get());
                const bool leaf = c->type == TrackType::Audio || c->type == TrackType::Midi;
                if (leaf && c->delay)
                    delayWriteAdd(*c->delay, c->shaped, buf, std::max(0, maxRoute - routeOf(*g, c.get())));
                else
                    buf.addFrom(c->shaped);
            }
        } else if (n->type == TrackType::Audio || n->type == TrackType::Midi) {
            const bool play = playing_.load();
            const double q = playhead_.load();
            n->midiIn.clear();
            if (play) {
                const int scene = n->sessionScene.load();
                if (scene >= 0 && scene < static_cast<int>(n->slots.size()) && n->slots[scene]) {
                    const Clip& clip = *n->slots[scene];
                    const double local = std::fmod(std::max(0.0, q - n->sessionOrigin.load()), std::max(0.01, clip.lengthQ));
                    if (!clip.mediaId.empty()) {
                        auto it = g->media.find(clip.mediaId);
                        if (it != g->media.end() && it->second) {
                            const auto& m = *it->second;
                            const double start = (local + clip.offsetQ) * spq * (m.sampleRate / sampleRate_);
                            for (int i = 0; i < frames; ++i) {
                                const int idx = static_cast<int>(start + i * m.sampleRate / sampleRate_);
                                if (idx < 0 || idx >= static_cast<int>(m.interleaved.size() / m.channels))
                                    continue;
                                for (int c = 0; c < 2; ++c)
                                    buf.ch(c)[i] += m.interleaved[static_cast<size_t>(idx) * m.channels + std::min(c, m.channels - 1)];
                            }
                        }
                    }
                    if (n->type == TrackType::Midi) {
                        for (const auto& note : clip.notes) {
                            const double on = note.startQ;
                            const double off = note.startQ + note.lengthQ;
                            const int onS = static_cast<int>((on - local) * spq);
                            const int offS = static_cast<int>((off - local) * spq);
                            if (onS >= 0 && onS < frames)
                                n->midiIn.add(onS, 0x90, static_cast<uint8_t>(note.pitch), static_cast<uint8_t>(note.velocity * 127));
                            if (offS >= 0 && offS < frames)
                                n->midiIn.add(offS, 0x80, static_cast<uint8_t>(note.pitch), 0);
                        }
                    }
                } else {
                    for (const auto& clip : n->clips) {
                        if (q + frames / spq < clip.startQ || q > clip.startQ + clip.lengthQ)
                            continue;
                        if (!clip.mediaId.empty()) {
                            auto it = g->media.find(clip.mediaId);
                            if (it == g->media.end() || !it->second)
                                continue;
                            const auto& m = *it->second;
                            for (int i = 0; i < frames; ++i) {
                                const double qq = q + i / spq;
                                if (qq < clip.startQ || qq >= clip.startQ + clip.lengthQ)
                                    continue;
                                const double local = (qq - clip.startQ + clip.offsetQ) * spq * (m.sampleRate / sampleRate_);
                                const int idx = static_cast<int>(local);
                                const int total = static_cast<int>(m.interleaved.size() / std::max(1, m.channels));
                                if (idx < 0 || total <= 0)
                                    continue;
                                const int wrapped = idx % total;
                                for (int c = 0; c < 2; ++c)
                                    buf.ch(c)[i] += m.interleaved[static_cast<size_t>(wrapped) * m.channels + std::min(c, m.channels - 1)];
                            }
                        }
                        for (const auto& note : clip.notes) {
                            const double onQ = clip.startQ + note.startQ;
                            const double offQ = onQ + note.lengthQ;
                            const int onS = static_cast<int>((onQ - q) * spq);
                            const int offS = static_cast<int>((offQ - q) * spq);
                            if (onS >= 0 && onS < frames)
                                n->midiIn.add(onS, 0x90, static_cast<uint8_t>(note.pitch), static_cast<uint8_t>(note.velocity * 127));
                            if (offS >= 0 && offS < frames)
                                n->midiIn.add(offS, 0x80, static_cast<uint8_t>(note.pitch), 0);
                        }
                    }
                }
            }
            if (n->arm.load() && interleavedIn) {
                for (int i = 0; i < frames; ++i) {
                    buf.ch(0)[i] += interleavedIn[i * 2];
                    buf.ch(1)[i] += interleavedIn[i * 2 + 1];
                }
                if (play && recording_.load()) {
                    if (RecSlot* rec = n->rec.load(std::memory_order_acquire)) {
                        int pos = rec->used.load(std::memory_order_relaxed);
                        if (pos + frames <= static_cast<int>(rec->samples.size()) / 2) {
                            float* dst = rec->samples.data() + static_cast<size_t>(pos) * 2;
                            std::memcpy(dst, interleavedIn, static_cast<size_t>(frames) * 2 * sizeof(float));
                            rec->used.store(pos + frames, std::memory_order_release);
                        }
                    }
                }
            }
            if (n->type == TrackType::Midi && n->arm.load()) {
                int r = midiR_.load(std::memory_order_relaxed);
                const int w = midiW_.load(std::memory_order_acquire);
                while (r != w && n->midiIn.events.size() < n->midiIn.events.capacity()) {
                    const LiveMidi e = midiRing_[r];
                    int off = e.offset;
                    if (off < 0)
                        off = 0;
                    if (off >= frames)
                        off = frames - 1;
                    n->midiIn.add(off, e.status, e.d1, e.d2);
                    r = (r + 1) & 255;
                }
                midiR_.store(r, std::memory_order_release);
            }
            if (n->type == TrackType::Midi) {
                fitBuffer(n->shaped, frames);
                for (auto& d : n->devices)
                    if (!d->bypass.load())
                        d->process(n->shaped, n->midiIn);
                buf.addFrom(n->shaped);
            } else {
                for (auto& d : n->devices)
                    if (!d->bypass.load())
                        d->process(buf, silentMidi_);
            }
        }

        fitBuffer(n->shaped, frames);
        AudioBuffer& post = n->shaped;
        const bool audible = !n->mute.load() && !ancestorMuted(*g, n) &&
                             (!anySolo || n->solo.load() || ancestorSolo(*g, n) || descendantSolo(*g, n) ||
                              n->type == TrackType::Return);
        float vol = audible ? dbToGain(n->volumeDb.load()) : 0.f;
        float gL = 1.f, gR = 1.f;
        panGains(n->pan.load(), gL, gR);
        for (int i = 0; i < frames; ++i) {
            post.ch(0)[i] = buf.ch(0)[i] * vol * gL;
            post.ch(1)[i] = buf.ch(1)[i] * vol * gR;
        }
        if (audible) {
            for (auto& s : n->sends) {
                auto* ret = g->find(s->target);
                if (!ret)
                    continue;
                const float sg = dbToGain(s->db.load());
                const AudioBuffer& tap = s->pre.load() ? buf : post;
                ret->work.addFrom(tap, sg);
            }
        }
        rememberPeak(n, post.peak());
    };

    for (const auto& apply : g->autos) {
        if (!apply.node)
            continue;
        const float v = evalPoints(apply.points, playhead_.load());
        if (apply.kind == 0)
            apply.node->volumeDb.store(v);
        else
            apply.node->pan.store(v);
    }

    for (auto& n : g->nodes) {
        if (n->type == TrackType::Return || n->type == TrackType::Master)
            continue;
        if (!n->output.empty() && n->output != "master")
            continue;
        render(render, n.get());
        const bool leaf = n->type == TrackType::Audio || n->type == TrackType::Midi;
        if (leaf && n->delay)
            delayWriteAdd(*n->delay, n->shaped, mix_, std::max(0, maxRoute - routeOf(*g, n.get())));
        else
            mix_.addFrom(n->shaped);
    }
    for (auto& n : g->nodes) {
        if (n->type != TrackType::Return)
            continue;
        AudioBuffer& buf = n->work;
        for (auto& d : n->devices)
            if (!d->bypass.load())
                d->process(buf, silentMidi_);
        const float vol = dbToGain(n->volumeDb.load());
        for (int i = 0; i < frames; ++i) {
            buf.ch(0)[i] *= vol;
            buf.ch(1)[i] *= vol;
        }
        if (n->delay)
            delayWriteAdd(*n->delay, buf, mix_, std::max(0, maxRoute - routeOf(*g, n.get())));
        else
            mix_.addFrom(buf);
        rememberPeak(n.get(), buf.peak());
    }
    if (auto* mnode = g->find("master")) {
        for (auto& d : mnode->devices)
            if (!d->bypass.load())
                d->process(mix_, silentMidi_);
        const float vol = dbToGain(mnode->volumeDb.load());
        float gL = 1.f, gR = 1.f;
        // Channel strips use equal-power pan. The master at center stays at unity
        // so a centered mix is not attenuated a second time.
        if (std::abs(mnode->pan.load()) > 1.0e-4f)
            panGains(mnode->pan.load(), gL, gR);
        for (int i = 0; i < frames; ++i) {
            mix_.ch(0)[i] *= vol * gL;
            mix_.ch(1)[i] *= vol * gR;
        }
        rememberPeak(mnode, mix_.peak());
    }

    if (g->metronome && playing_.load()) {
        const double beatQ = 4.0 / std::max(1, g->denominator);
        const double q0 = playhead_.load();
        const double q1 = q0 + frames / spq;
        for (double b = std::ceil(q0 / beatQ) * beatQ; b < q1 + 1e-9; b += beatQ) {
            int s = static_cast<int>((b - q0) * spq);
            if (s < 0 || s >= frames)
                continue;
            const bool accent = std::fmod(b / beatQ, g->numerator) < 0.01;
            const float freq = accent ? 1800.f : 1000.f;
            const int len = std::min(frames - s, static_cast<int>(0.02 * sampleRate_));
            for (int i = 0; i < len; ++i) {
                const float env = 1.f - static_cast<float>(i) / len;
                const float y = std::sin(6.2831853f * freq * i / static_cast<float>(sampleRate_)) * env * g->metronomeLevel;
                mix_.ch(0)[s + i] += y;
                mix_.ch(1)[s + i] += y;
            }
        }
    }

    if (interleavedOut) {
        for (int i = 0; i < frames; ++i) {
            interleavedOut[i * 2] = mix_.ch(0)[i];
            interleavedOut[i * 2 + 1] = mix_.ch(1)[i];
        }
    }
    if (playing_.load()) {
        double q = playhead_.load() + frames / spq;
        if (g->loop && q >= g->loopEnd)
            q = g->loopStart + std::fmod(q - g->loopStart, std::max(0.01, g->loopEnd - g->loopStart));
        playhead_.store(q);
    }
    releaseGraph();
}

void Engine::pushMidi(int sampleOffset, uint8_t status, uint8_t data1, uint8_t data2) {
    const int w = midiW_.load(std::memory_order_relaxed);
    const int next = (w + 1) & 255;
    if (next == midiR_.load(std::memory_order_acquire))
        return;
    midiRing_[w] = LiveMidi {sampleOffset, status, data1, data2};
    midiW_.store(next, std::memory_order_release);
}

void Engine::play() { playing_.store(true); }
void Engine::stop() {
    playing_.store(false);
    recording_.store(false);
    finalizeTakes();
}
void Engine::setRecording(bool recording) {
    if (recording && project_) {
        Graph* g = acquire();
        const int capFrames = static_cast<int>(sampleRate_) * 60;
        for (const auto& t : project_->tracks) {
            if (!t.arm)
                continue;
            auto slot = std::make_unique<RecSlot>();
            slot->samples.assign(static_cast<size_t>(capFrames) * 2, 0.f);
            slot->startQ = playhead_.load();
            slot->rate = static_cast<int>(sampleRate_);
            RecSlot* raw = slot.get();
            recs_[t.id] = std::move(slot);
            if (g)
                if (auto* n = g->find(t.id))
                    n->rec.store(raw, std::memory_order_release);
        }
        releaseGraph();
    }
    recording_.store(recording);
}
void Engine::seek(double quarters) {
    playhead_.store(quarters);
    if (project_)
        project_->playhead = quarters;
}

float Engine::meter(const std::string& trackId) const {
    Graph* g = acquire();
    float v = 0.f;
    if (g)
        for (auto& n : g->nodes)
            if (n->id == trackId)
                v = n->meter.load();
    releaseGraph();
    return v;
}
float Engine::holdMeter(const std::string& trackId) const {
    Graph* g = acquire();
    float v = 0.f;
    if (g)
        for (auto& n : g->nodes)
            if (n->id == trackId)
                v = n->hold.load();
    releaseGraph();
    return v;
}
bool Engine::clipLamp(const std::string& trackId) const {
    Graph* g = acquire();
    bool v = false;
    if (g)
        for (auto& n : g->nodes)
            if (n->id == trackId)
                v = n->clipLamp.load() != 0;
    releaseGraph();
    return v;
}

static Node* liveNode(Engine::Graph* g, const std::string& id) {
    if (!g)
        return nullptr;
    return g->find(id);
}

void Engine::setVolumeDb(const std::string& track, float db) {
    Graph* g = acquire();
    if (auto* n = liveNode(g, track))
        n->volumeDb.store(db);
    releaseGraph();
    if (project_)
        if (auto* t = project_->findTrack(track))
            t->volumeDb = db;
}
void Engine::setPan(const std::string& track, float pan) {
    Graph* g = acquire();
    if (auto* n = liveNode(g, track))
        n->pan.store(pan);
    releaseGraph();
    if (project_)
        if (auto* t = project_->findTrack(track))
            t->pan = pan;
}
void Engine::setMute(const std::string& track, bool v) {
    Graph* g = acquire();
    if (auto* n = liveNode(g, track))
        n->mute.store(v);
    releaseGraph();
    if (project_)
        if (auto* t = project_->findTrack(track))
            t->mute = v;
}
void Engine::setSolo(const std::string& track, bool v) {
    Graph* g = acquire();
    if (auto* n = liveNode(g, track))
        n->solo.store(v);
    releaseGraph();
    if (project_)
        if (auto* t = project_->findTrack(track))
            t->solo = v;
}
void Engine::setArm(const std::string& track, bool v) {
    Graph* g = acquire();
    if (auto* n = liveNode(g, track))
        n->arm.store(v);
    releaseGraph();
    if (project_)
        if (auto* t = project_->findTrack(track))
            t->arm = v;
}
void Engine::setSend(const std::string& track, const std::string& target, float db, bool pre) {
    Graph* g = acquire();
    if (auto* n = liveNode(g, track)) {
        for (auto& s : n->sends)
            if (s->target == target) {
                s->db.store(db);
                s->pre.store(pre);
            }
    }
    releaseGraph();
    if (project_)
        if (auto* t = project_->findTrack(track))
            for (auto& s : t->sends)
                if (s.target == target) {
                    s.levelDb = db;
                    s.pre = pre;
                }
}
void Engine::addDevice(const std::string& track, const std::string& type) {
    if (!project_)
        return;
    auto* t = project_->findTrack(track);
    if (!t)
        return;
    DeviceState d;
    d.id = project_->newId("d");
    d.type = type;
    d.name = deviceDisplayName(type);
    t->devices.push_back(d);
    publish();
}
bool Engine::addPlugin(const std::string& track, const std::string& path, std::string& error) {
    if (!project_ || !plugins_) {
        error = "no plugin host";
        return false;
    }
    if (!plugins_->scan(path, error))
        return false;
    auto* t = project_->findTrack(track);
    if (!t) {
        error = "missing track";
        return false;
    }
    DeviceState d;
    d.id = project_->newId("p");
    d.type = "vst3";
    d.name = plugins_->lastName().empty() ? "VST3" : plugins_->lastName();
    d.pluginPath = path;
    t->devices.push_back(d);
    publish();
    return true;
}
void Engine::setBypass(const std::string& deviceId, bool bypass) {
    if (auto* d = device(deviceId))
        d->bypass.store(bypass);
    if (project_)
        if (auto* s = project_->findDevice(deviceId))
            s->bypass = bypass;
}
void Engine::setDeviceParam(const std::string& deviceId, const std::string& param, float value) {
    if (auto* d = device(deviceId))
        d->set(param, value);
    if (project_)
        if (auto* s = project_->findDevice(deviceId))
            s->params[param] = value;
}
Device* Engine::device(const std::string& id) {
    Graph* g = acquire();
    Device* found = nullptr;
    if (g)
        for (auto& n : g->nodes)
            for (auto& d : n->devices)
                if (d->id == id)
                    found = d.get();
    releaseGraph();
    return found;
}
void Engine::launchScene(int scene) {
    Graph* g = acquire();
    if (g) {
        for (auto& n : g->nodes) {
            if (scene >= 0 && scene < static_cast<int>(n->slots.size()) && n->slots[scene]) {
                n->sessionScene.store(scene);
                n->sessionOrigin.store(playhead_.load());
            }
        }
    }
    releaseGraph();
    play();
}
void Engine::launchSlot(const std::string& track, int scene) {
    Graph* g = acquire();
    if (auto* n = liveNode(g, track)) {
        n->sessionScene.store(scene);
        n->sessionOrigin.store(playhead_.load());
    }
    releaseGraph();
    play();
}

void Engine::finalizeTakes() {
    if (!project_)
        return;
    Graph* g = acquire();
    if (g)
        for (auto& n : g->nodes)
            n->rec.store(nullptr, std::memory_order_release);
    releaseGraph();
    while (readers_.load(std::memory_order_acquire) != 0) {
    }
    for (auto& kv : recs_) {
        RecSlot& take = *kv.second;
        const int frames = take.used.load(std::memory_order_acquire);
        if (frames <= 0)
            continue;
        auto media = std::make_shared<Media>();
        media->id = project_->newId("rec");
        media->sampleRate = take.rate;
        media->channels = 2;
        media->interleaved.assign(take.samples.begin(), take.samples.begin() + static_cast<std::ptrdiff_t>(frames) * 2);
        project_->media[media->id] = media;
        if (auto* t = project_->findTrack(kv.first)) {
            Clip c;
            c.id = project_->newId("c");
            c.name = "Take";
            c.startQ = take.startQ;
            c.lengthQ = frames / (sampleRate_ * 60.0 / std::max(1.0, project_->bpm));
            c.mediaId = media->id;
            t->clips.push_back(c);
        }
    }
    recs_.clear();
    publish();
}

bool Engine::saveProject(const std::string& folder) {
    if (!project_)
        return false;
    project_->playhead = playhead_.load();
    return project_->saveToFolder(folder);
}
bool Engine::loadProject(const std::string& folder) {
    auto p = std::make_shared<Project>();
    if (!p->loadFromFolder(folder))
        return false;
    setProject(p);
    return true;
}
bool Engine::renderToWav(const std::string& path) {
    if (!project_)
        return false;
    stop();
    auto copy = std::make_shared<Project>(*project_);
    copy->metronome = false;
    Engine ghost;
    ghost.offline_ = true;
    ghost.plugins_ = plugins_;
    ghost.sampleRate_ = sampleRate_;
    ghost.block_ = block_;
    ghost.setProject(copy);
    ghost.prepare(sampleRate_, block_);
    ghost.seek(0);
    ghost.play();
    const double spq = sampleRate_ * 60.0 / std::max(1.0, copy->bpm);
    const int total = static_cast<int>((copy->loopEnd + 0.25 * copy->bpm / 60.0) * spq);
    std::vector<float> interleaved(static_cast<size_t>(total) * 2);
    std::vector<float> block(static_cast<size_t>(block_) * 2);
    int done = 0;
    while (done < total) {
        const int n = std::min(block_, total - done);
        std::fill(block.begin(), block.end(), 0.f);
        ghost.process(block.data(), nullptr, n);
        std::copy(block.begin(), block.begin() + n * 2, interleaved.begin() + static_cast<size_t>(done) * 2);
        done += n;
    }
    return writeWav24(path, interleaved.data(), 2, total, static_cast<int>(sampleRate_));
}

} // namespace ghovlz
