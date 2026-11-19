#pragma once

#include "model/Project.h"

#include <string>
#include <vector>

namespace ghovlz {

struct BrowserItem {
    enum class Kind { Device, Sample, Plugin };
    Kind kind = Kind::Device;
    std::string id;
    std::string label;
};

bool browserMatches(const std::string& label, const std::string& query);
std::vector<BrowserItem> browserCatalog(const Project& project);
std::vector<BrowserItem> browserFiltered(const Project& project, const std::string& query);

Clip* findClip(Project& project, const std::string& trackId, const std::string& clipId);
const Clip* findClip(const Project& project, const std::string& trackId, const std::string& clipId);

bool moveClip(Project& project, const std::string& trackId, const std::string& clipId, double startQ);
bool resizeClip(Project& project, const std::string& trackId, const std::string& clipId, double lengthQ);
bool slipClip(Project& project, const std::string& trackId, const std::string& clipId, double deltaOffsetQ);
bool splitClip(Project& project, const std::string& trackId, const std::string& clipId, double atQ);
bool duplicateClip(Project& project, const std::string& trackId, const std::string& clipId);
bool moveClips(Project& project, const std::vector<std::string>& clipIds, double deltaQ);
bool setClipGain(Project& project, const std::string& trackId, const std::string& clipId, float gainDb);
bool setClipFades(Project& project, const std::string& trackId, const std::string& clipId, double fadeInQ, double fadeOutQ);
bool setClipWarp(Project& project, const std::string& trackId, const std::string& clipId, WarpMode mode);

bool addAutoPoint(Project& project, const std::string& laneId, double quarter, float value);
bool moveAutoPoint(Project& project, const std::string& laneId, int index, double quarter, float value);
bool eraseAutoPoint(Project& project, const std::string& laneId, int index);

bool addNote(Project& project, const std::string& trackId, const std::string& clipId, const Note& note);
bool moveNote(Project& project, const std::string& trackId, const std::string& clipId, int index, double startQ, int pitch);
bool resizeNote(Project& project, const std::string& trackId, const std::string& clipId, int index, double lengthQ);
bool setNoteVelocity(Project& project, const std::string& trackId, const std::string& clipId, int index, float velocity);
int quantizeNotes(Project& project, const std::string& trackId, const std::string& clipId, double gridQ, float strength);

bool dropOnTrack(Project& project, const BrowserItem& item, const std::string& trackId, double startQ);
bool dropOnEmpty(Project& project, const BrowserItem& item, double startQ);

std::vector<float> previewMedia(const Media& media, int frames, int startFrame);

} // namespace ghovlz
