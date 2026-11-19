#include "edit/Edits.h"

#include "core/Types.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ghovlz {
namespace {

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Track* trackById(Project& project, const std::string& id) {
    if (project.master.id == id)
        return &project.master;
    for (auto& t : project.tracks)
        if (t.id == id)
            return &t;
    return nullptr;
}

const Track* trackById(const Project& project, const std::string& id) {
    if (project.master.id == id)
        return &project.master;
    for (const auto& t : project.tracks)
        if (t.id == id)
            return &t;
    return nullptr;
}

Clip* clipOn(Track& track, const std::string& clipId) {
    for (auto& c : track.clips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

Note* noteAt(Clip& clip, int index) {
    if (index < 0 || index >= static_cast<int>(clip.notes.size()))
        return nullptr;
    return &clip.notes[static_cast<size_t>(index)];
}

Lane* laneById(Project& project, const std::string& id) {
    for (auto& lane : project.automation)
        if (lane.id == id)
            return &lane;
    return nullptr;
}

} // namespace

bool browserMatches(const std::string& label, const std::string& query) {
    if (query.empty())
        return true;
    return lower(label).find(lower(query)) != std::string::npos;
}

std::vector<BrowserItem> browserCatalog(const Project& project) {
    std::vector<BrowserItem> items;
    const char* devices[] = {"eq", "dyn", "filter", "saturator", "delay", "reverb", "utility",
                             "wavetable", "sampler", "drumrack", "arpeggiator"};
    for (const char* name : devices)
        items.push_back({BrowserItem::Kind::Device, name, name});
    for (const auto& m : project.media)
        items.push_back({BrowserItem::Kind::Sample, m.id, m.name.empty() ? m.id : m.name});
    items.push_back({BrowserItem::Kind::Plugin, "vst3", "VST3"});
    return items;
}

std::vector<BrowserItem> browserFiltered(const Project& project, const std::string& query) {
    std::vector<BrowserItem> out;
    for (const auto& item : browserCatalog(project))
        if (browserMatches(item.label, query) || browserMatches(item.id, query))
            out.push_back(item);
    return out;
}

Clip* findClip(Project& project, const std::string& trackId, const std::string& clipId) {
    Track* track = trackById(project, trackId);
    return track ? clipOn(*track, clipId) : nullptr;
}

const Clip* findClip(const Project& project, const std::string& trackId, const std::string& clipId) {
    const Track* track = trackById(project, trackId);
    if (!track)
        return nullptr;
    for (const auto& c : track->clips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

bool moveClip(Project& project, const std::string& trackId, const std::string& clipId, double startQ) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    clip->startQ = snapQuarter(std::max(0.0, startQ), project.gridQ);
    return true;
}

bool resizeClip(Project& project, const std::string& trackId, const std::string& clipId, double lengthQ) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    clip->lengthQ = std::max(0.25, snapQuarter(lengthQ, project.gridQ));
    return true;
}

bool slipClip(Project& project, const std::string& trackId, const std::string& clipId, double deltaOffsetQ) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    clip->offsetQ = std::max(0.0, clip->offsetQ + deltaOffsetQ);
    return true;
}

bool splitClip(Project& project, const std::string& trackId, const std::string& clipId, double atQ) {
    Track* track = trackById(project, trackId);
    if (!track)
        return false;
    Clip* clip = clipOn(*track, clipId);
    if (!clip)
        return false;
    const double cut = snapQuarter(atQ, project.gridQ);
    if (cut <= clip->startQ + 0.01 || cut >= clip->startQ + clip->lengthQ - 0.01)
        return false;
    Clip right = *clip;
    const double leftLen = cut - clip->startQ;
    right.id = project.newId("clip");
    right.startQ = cut;
    right.lengthQ = clip->lengthQ - leftLen;
    right.offsetQ = clip->offsetQ + leftLen;
    right.name += " b";
    clip->lengthQ = leftLen;
    track->clips.push_back(std::move(right));
    return true;
}

bool duplicateClip(Project& project, const std::string& trackId, const std::string& clipId) {
    Track* track = trackById(project, trackId);
    if (!track)
        return false;
    Clip* clip = clipOn(*track, clipId);
    if (!clip)
        return false;
    Clip copy = *clip;
    copy.id = project.newId("clip");
    copy.startQ = clip->startQ + clip->lengthQ;
    copy.name += " copy";
    track->clips.push_back(std::move(copy));
    return true;
}

bool moveClips(Project& project, const std::vector<std::string>& clipIds, double deltaQ) {
    bool any = false;
    const double step = snapQuarter(deltaQ, project.gridQ);
    for (auto& track : project.tracks) {
        for (auto& clip : track.clips) {
            if (std::find(clipIds.begin(), clipIds.end(), clip.id) == clipIds.end())
                continue;
            clip.startQ = std::max(0.0, clip.startQ + step);
            any = true;
        }
    }
    return any;
}

bool setClipGain(Project& project, const std::string& trackId, const std::string& clipId, float gainDb) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    clip->gainDb = std::clamp(gainDb, -48.f, 24.f);
    return true;
}

bool setClipFades(Project& project, const std::string& trackId, const std::string& clipId, double fadeInQ, double fadeOutQ) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    clip->fadeInQ = std::clamp(fadeInQ, 0.0, clip->lengthQ);
    clip->fadeOutQ = std::clamp(fadeOutQ, 0.0, clip->lengthQ);
    return true;
}

bool setClipWarp(Project& project, const std::string& trackId, const std::string& clipId, WarpMode mode) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    clip->warp = mode;
    return true;
}

bool addAutoPoint(Project& project, const std::string& laneId, double quarter, float value) {
    Lane* lane = laneById(project, laneId);
    if (!lane)
        return false;
    AutoPoint point;
    point.quarter = snapQuarter(std::max(0.0, quarter), project.gridQ);
    point.value = std::clamp(value, 0.f, 1.f);
    lane->points.push_back(point);
    std::sort(lane->points.begin(), lane->points.end(), [](const AutoPoint& a, const AutoPoint& b) {
        return a.quarter < b.quarter;
    });
    return true;
}

bool moveAutoPoint(Project& project, const std::string& laneId, int index, double quarter, float value) {
    Lane* lane = laneById(project, laneId);
    if (!lane || index < 0 || index >= static_cast<int>(lane->points.size()))
        return false;
    lane->points[static_cast<size_t>(index)].quarter = snapQuarter(std::max(0.0, quarter), project.gridQ);
    lane->points[static_cast<size_t>(index)].value = std::clamp(value, 0.f, 1.f);
    return true;
}

bool eraseAutoPoint(Project& project, const std::string& laneId, int index) {
    Lane* lane = laneById(project, laneId);
    if (!lane || index < 0 || index >= static_cast<int>(lane->points.size()))
        return false;
    lane->points.erase(lane->points.begin() + index);
    return true;
}

bool addNote(Project& project, const std::string& trackId, const std::string& clipId, const Note& note) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    Note copy = note;
    copy.startQ = snapQuarter(std::max(0.0, note.startQ), project.gridQ);
    copy.lengthQ = std::max(0.05, note.lengthQ);
    copy.pitch = std::clamp(note.pitch, 0, 127);
    copy.velocity = std::clamp(note.velocity, 0.f, 1.f);
    clip->notes.push_back(copy);
    return true;
}

bool moveNote(Project& project, const std::string& trackId, const std::string& clipId, int index, double startQ, int pitch) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    Note* note = noteAt(*clip, index);
    if (!note)
        return false;
    note->startQ = snapQuarter(std::max(0.0, startQ), project.gridQ);
    note->pitch = std::clamp(pitch, 0, 127);
    return true;
}

bool resizeNote(Project& project, const std::string& trackId, const std::string& clipId, int index, double lengthQ) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    Note* note = noteAt(*clip, index);
    if (!note)
        return false;
    note->lengthQ = std::max(0.05, snapQuarter(lengthQ, project.gridQ));
    return true;
}

bool setNoteVelocity(Project& project, const std::string& trackId, const std::string& clipId, int index, float velocity) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip)
        return false;
    Note* note = noteAt(*clip, index);
    if (!note)
        return false;
    note->velocity = std::clamp(velocity, 0.f, 1.f);
    return true;
}

int quantizeNotes(Project& project, const std::string& trackId, const std::string& clipId, double gridQ, float strength) {
    Clip* clip = findClip(project, trackId, clipId);
    if (!clip || gridQ <= 1e-9)
        return 0;
    const float amount = std::clamp(strength, 0.f, 1.f);
    int moved = 0;
    for (auto& note : clip->notes) {
        const double target = snapQuarter(note.startQ, gridQ);
        const double next = note.startQ + (target - note.startQ) * amount;
        if (std::abs(next - note.startQ) > 1e-6)
            ++moved;
        note.startQ = std::max(0.0, next);
    }
    return moved;
}

bool dropOnTrack(Project& project, const BrowserItem& item, const std::string& trackId, double startQ) {
    Track* track = trackById(project, trackId);
    if (!track)
        return false;
    if (item.kind == BrowserItem::Kind::Device) {
        if (track->type == TrackType::Group)
            return false;
        DeviceState state;
        state.id = project.newId("dev");
        state.type = item.id;
        state.name = item.label;
        track->devices.push_back(std::move(state));
        return true;
    }
    if (item.kind == BrowserItem::Kind::Sample) {
        if (track->type != TrackType::Audio && track->type != TrackType::Midi)
            return false;
        const Media* media = nullptr;
        for (const auto& m : project.media)
            if (m.id == item.id)
                media = &m;
        if (!media)
            return false;
        Clip clip;
        clip.id = project.newId("clip");
        clip.name = media->name.empty() ? media->id : media->name;
        clip.startQ = snapQuarter(std::max(0.0, startQ), project.gridQ);
        const double seconds = media->channels > 0
            ? static_cast<double>(media->interleaved.size() / static_cast<size_t>(media->channels)) / std::max(1, media->sampleRate)
            : 1.0;
        clip.lengthQ = std::max(0.25, seconds * project.bpm / 60.0);
        clip.mediaId = media->id;
        track->clips.push_back(std::move(clip));
        return true;
    }
    if (item.kind == BrowserItem::Kind::Plugin) {
        DeviceState state;
        state.id = project.newId("dev");
        state.type = "vst3";
        state.name = item.label;
        track->devices.push_back(std::move(state));
        return true;
    }
    return false;
}

bool dropOnEmpty(Project& project, const BrowserItem& item, double startQ) {
    Track track;
    track.id = project.newId("track");
    track.name = item.kind == BrowserItem::Kind::Device ? item.label : item.label;
    track.type = item.kind == BrowserItem::Kind::Sample ? TrackType::Audio : TrackType::Midi;
    track.color = 0xFF3DDC97;
    project.tracks.push_back(std::move(track));
    return dropOnTrack(project, item, project.tracks.back().id, startQ);
}

std::vector<float> previewMedia(const Media& media, int frames, int startFrame) {
    std::vector<float> out(static_cast<size_t>(std::max(0, frames)), 0.f);
    if (media.channels <= 0 || frames <= 0)
        return out;
    const int total = static_cast<int>(media.interleaved.size() / static_cast<size_t>(media.channels));
    for (int i = 0; i < frames; ++i) {
        const int idx = startFrame + i;
        if (idx < 0 || idx >= total)
            continue;
        out[static_cast<size_t>(i)] = media.interleaved[static_cast<size_t>(idx) * media.channels];
    }
    return out;
}

} // namespace ghovlz
