#include "model/Edit.h"

#include <algorithm>
#include <cmath>

namespace ghovlz {
namespace {

Clip* findClip(Project& project, const std::string& track, const std::string& clipId) {
    Track* t = project.findTrack(track);
    if (!t)
        return nullptr;
    for (auto& c : t->clips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

} // namespace

double mapWarp(const std::vector<WarpMark>& marks, double outputQ) {
    if (marks.size() < 2)
        return outputQ;
    if (outputQ <= marks.front().outputQ)
        return marks.front().sourceQ;
    for (size_t i = 1; i < marks.size(); ++i) {
        const double span = marks[i].outputQ - marks[i - 1].outputQ;
        if (outputQ <= marks[i].outputQ && span > 1e-9) {
            const double t = (outputQ - marks[i - 1].outputQ) / span;
            return marks[i - 1].sourceQ + t * (marks[i].sourceQ - marks[i - 1].sourceQ);
        }
    }
    return marks.back().sourceQ;
}

bool dropMedia(Project& project, const std::string& track, double quarter, const std::string& mediaId, double lengthQ) {
    Track* t = project.findTrack(track);
    if (!t || mediaId.empty() || !(lengthQ > 0.0))
        return false;
    Clip c;
    c.id = project.newId("clip");
    c.name = mediaId;
    c.startQ = quarter;
    c.lengthQ = lengthQ;
    c.mediaId = mediaId;
    c.color = t->color;
    t->clips.push_back(std::move(c));
    return true;
}

bool splitClip(Project& project, const std::string& track, const std::string& clipId, double quarter) {
    Track* t = project.findTrack(track);
    Clip* c = findClip(project, track, clipId);
    if (!t || !c)
        return false;
    if (!(quarter > c->startQ) || !(quarter < c->startQ + c->lengthQ))
        return false;
    const double local = quarter - c->startQ;
    Clip right = *c;
    right.id = project.newId("clip");
    right.startQ = quarter;
    right.lengthQ = c->lengthQ - local;
    right.offsetQ = c->offsetQ + local;
    right.notes.clear();
    std::vector<Note> leftNotes;
    for (const auto& n : c->notes) {
        if (n.startQ >= local) {
            Note moved = n;
            moved.startQ -= local;
            right.notes.push_back(moved);
        } else {
            Note kept = n;
            if (kept.startQ + kept.lengthQ > local)
                kept.lengthQ = local - kept.startQ;
            leftNotes.push_back(kept);
        }
    }
    c->lengthQ = local;
    c->notes = std::move(leftNotes);
    t->clips.push_back(std::move(right));
    return true;
}

bool resizeClip(Project& project, const std::string& track, const std::string& clipId, double lengthQ) {
    Clip* c = findClip(project, track, clipId);
    if (!c || !(lengthQ > 0.0))
        return false;
    c->lengthQ = lengthQ;
    c->notes.erase(std::remove_if(c->notes.begin(), c->notes.end(),
                                  [lengthQ](const Note& n) { return n.startQ >= lengthQ; }),
                   c->notes.end());
    return true;
}

bool duplicateClip(Project& project, const std::string& track, const std::string& clipId) {
    Track* t = project.findTrack(track);
    Clip* c = findClip(project, track, clipId);
    if (!t || !c)
        return false;
    Clip copy = *c;
    copy.id = project.newId("clip");
    copy.startQ = c->startQ + c->lengthQ;
    t->clips.push_back(std::move(copy));
    return true;
}

bool slipClip(Project& project, const std::string& track, const std::string& clipId, double offsetQ) {
    Clip* c = findClip(project, track, clipId);
    if (!c)
        return false;
    c->offsetQ = offsetQ;
    return true;
}

bool addNote(Project& project, const std::string& track, const std::string& clipId, const Note& note) {
    Clip* c = findClip(project, track, clipId);
    if (!c || !(note.lengthQ > 0.0))
        return false;
    c->notes.push_back(note);
    return true;
}

bool deleteNote(Project& project, const std::string& track, const std::string& clipId, int index) {
    Clip* c = findClip(project, track, clipId);
    if (!c || index < 0 || index >= static_cast<int>(c->notes.size()))
        return false;
    c->notes.erase(c->notes.begin() + index);
    return true;
}

bool moveNote(Project& project, const std::string& track, const std::string& clipId, int index, double startQ, int pitch) {
    Clip* c = findClip(project, track, clipId);
    if (!c || index < 0 || index >= static_cast<int>(c->notes.size()))
        return false;
    c->notes[static_cast<size_t>(index)].startQ = startQ;
    c->notes[static_cast<size_t>(index)].pitch = pitch;
    return true;
}

int quantizeNotes(Project& project, const std::string& track, const std::string& clipId, double gridQ) {
    Clip* c = findClip(project, track, clipId);
    if (!c || !(gridQ > 0.0))
        return -1;
    int moved = 0;
    for (auto& n : c->notes) {
        const double snapped = std::round(n.startQ / gridQ) * gridQ;
        if (std::fabs(snapped - n.startQ) > 1e-9) {
            n.startQ = snapped;
            ++moved;
        }
    }
    return moved;
}

bool setWarp(Project& project, const std::string& track, const std::string& clipId, std::vector<WarpMark> marks) {
    Clip* c = findClip(project, track, clipId);
    if (!c)
        return false;
    std::sort(marks.begin(), marks.end(), [](const WarpMark& a, const WarpMark& b) { return a.outputQ < b.outputQ; });
    c->warp = std::move(marks);
    return true;
}

bool captureSlot(Project& project, const std::string& track, int scene) {
    Track* t = project.findTrack(track);
    if (!t || scene < 0 || t->clips.empty())
        return false;
    while (static_cast<int>(t->slots.size()) <= scene)
        t->slots.emplace_back(std::nullopt);
    t->slots[static_cast<size_t>(scene)] = t->clips.front();
    return true;
}

} // namespace ghovlz
