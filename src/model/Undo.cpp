#include "model/Undo.h"

#include "dsp/Devices.h"

#include <algorithm>
#include <cstring>

namespace ghovlz {
namespace {

void mix(uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
void mixS(uint64_t& h, const std::string& s) {
    for (unsigned char c : s)
        mix(h, c);
}
uint64_t bits(float v) {
    uint32_t u = 0;
    static_assert(sizeof(float) == 4, "float");
    std::memcpy(&u, &v, 4);
    return u;
}
uint64_t bitsD(double v) {
    uint64_t u = 0;
    static_assert(sizeof(double) == 8, "double");
    std::memcpy(&u, &v, 8);
    return u;
}

} // namespace

void UndoStack::apply(Project& project, const Cmd& cmd, bool inverse) {
    const float f = inverse ? cmd.before : cmd.after;
    const double d = inverse ? cmd.beforeD : cmd.afterD;
    if (cmd.kind == Cmd::Kind::Volume) {
        if (auto* t = project.findTrack(cmd.a))
            t->volumeDb = f;
    } else if (cmd.kind == Cmd::Kind::Param) {
        if (auto* dev = project.findDevice(cmd.a))
            dev->params[cmd.b] = f;
    } else if (cmd.kind == Cmd::Kind::Send) {
        if (auto* t = project.findTrack(cmd.a)) {
            for (auto& s : t->sends)
                if (s.target == cmd.b)
                    s.levelDb = f;
        }
    } else if (cmd.kind == Cmd::Kind::Move) {
        if (auto* t = project.findTrack(cmd.a))
            for (auto& c : t->clips)
                if (c.id == cmd.b)
                    c.startQ = d;
    } else if (cmd.kind == Cmd::Kind::Device) {
        if (auto* t = project.findTrack(cmd.a)) {
            if (!inverse) {
                DeviceState state;
                state.id = cmd.b;
                state.type = cmd.c;
                state.name = deviceDisplayName(cmd.c);
                t->devices.push_back(std::move(state));
            } else {
                t->devices.erase(std::remove_if(t->devices.begin(), t->devices.end(),
                                                [&](const DeviceState& s) { return s.id == cmd.b; }),
                                 t->devices.end());
            }
        }
    }
}

bool UndoStack::setVolume(Project& project, const std::string& track, float db) {
    auto* t = project.findTrack(track);
    if (!t || t->volumeDb == db)
        return false;
    Cmd cmd;
    cmd.kind = Cmd::Kind::Volume;
    cmd.a = track;
    cmd.before = t->volumeDb;
    cmd.after = db;
    apply(project, cmd, false);
    undo_.push_back(std::move(cmd));
    redo_.clear();
    return true;
}

bool UndoStack::setParam(Project& project, const std::string& device, const std::string& param, float value) {
    auto* d = project.findDevice(device);
    if (!d)
        return false;
    const float before = d->params.count(param) ? d->params[param] : 0.f;
    if (before == value)
        return false;
    Cmd cmd;
    cmd.kind = Cmd::Kind::Param;
    cmd.a = device;
    cmd.b = param;
    cmd.before = before;
    cmd.after = value;
    apply(project, cmd, false);
    undo_.push_back(std::move(cmd));
    redo_.clear();
    return true;
}

bool UndoStack::setSend(Project& project, const std::string& track, const std::string& target, float db) {
    auto* t = project.findTrack(track);
    if (!t)
        return false;
    float before = -100.f;
    bool found = false;
    for (const auto& s : t->sends)
        if (s.target == target) {
            before = s.levelDb;
            found = true;
        }
    if (!found || before == db)
        return false;
    Cmd cmd;
    cmd.kind = Cmd::Kind::Send;
    cmd.a = track;
    cmd.b = target;
    cmd.before = before;
    cmd.after = db;
    apply(project, cmd, false);
    undo_.push_back(std::move(cmd));
    redo_.clear();
    return true;
}

bool UndoStack::moveClip(Project& project, const std::string& track, const std::string& clip, double startQ) {
    auto* t = project.findTrack(track);
    if (!t)
        return false;
    Clip* found = nullptr;
    for (auto& c : t->clips)
        if (c.id == clip)
            found = &c;
    if (!found || found->startQ == startQ)
        return false;
    Cmd cmd;
    cmd.kind = Cmd::Kind::Move;
    cmd.a = track;
    cmd.b = clip;
    cmd.beforeD = found->startQ;
    cmd.afterD = startQ;
    apply(project, cmd, false);
    undo_.push_back(std::move(cmd));
    redo_.clear();
    return true;
}

bool UndoStack::addDevice(Project& project, const std::string& track, const std::string& type) {
    if (!project.findTrack(track))
        return false;
    Cmd cmd;
    cmd.kind = Cmd::Kind::Device;
    cmd.a = track;
    cmd.b = project.newId("undo");
    cmd.c = type;
    apply(project, cmd, false);
    undo_.push_back(std::move(cmd));
    redo_.clear();
    return true;
}

bool UndoStack::undo(Project& project) {
    if (undo_.empty())
        return false;
    Cmd cmd = undo_.back();
    undo_.pop_back();
    apply(project, cmd, true);
    redo_.push_back(std::move(cmd));
    return true;
}

bool UndoStack::redo(Project& project) {
    if (redo_.empty())
        return false;
    Cmd cmd = redo_.back();
    redo_.pop_back();
    apply(project, cmd, false);
    undo_.push_back(std::move(cmd));
    return true;
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
}

uint64_t projectHash(const Project& project) {
    uint64_t h = 1469598103934665603ull;
    mixS(h, project.name);
    mix(h, bitsD(project.bpm));
    mix(h, static_cast<uint64_t>(project.format));
    auto track = [&](const Track& t) {
        mixS(h, t.id);
        mix(h, bits(t.volumeDb));
        mix(h, bits(t.pan));
        mix(h, t.mute);
        mix(h, t.solo);
        mix(h, t.arm);
        for (const auto& d : t.devices) {
            mixS(h, d.id);
            mixS(h, d.type);
            for (const auto& kv : d.params) {
                mixS(h, kv.first);
                mix(h, bits(kv.second));
            }
        }
        for (const auto& c : t.clips) {
            mixS(h, c.id);
            mix(h, bitsD(c.startQ));
            mix(h, bitsD(c.lengthQ));
        }
        for (const auto& s : t.sends) {
            mixS(h, s.target);
            mix(h, bits(s.levelDb));
        }
    };
    track(project.master);
    for (const auto& t : project.tracks)
        track(t);
    for (const auto& lane : project.automation) {
        mixS(h, lane.id);
        for (const auto& p : lane.points) {
            mix(h, bitsD(p.quarter));
            mix(h, bits(p.value));
        }
    }
    return h;
}

} // namespace ghovlz
