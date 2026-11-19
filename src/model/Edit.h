#pragma once

#include "model/Project.h"

#include <string>
#include <vector>

namespace ghovlz {

// Timeline position inside a clip, mapped through warp marks onto source time.
// Fewer than two marks is identity. The audio thread calls this.
double mapWarp(const std::vector<WarpMark>& marks, double outputQ);

// BRW-01. Drops a media clip onto a track at a quarter.
bool dropMedia(Project& project, const std::string& track, double quarter, const std::string& mediaId, double lengthQ);

// ARR. Split at an absolute quarter, change length, or place a copy just after the clip.
bool splitClip(Project& project, const std::string& track, const std::string& clipId, double quarter);
bool resizeClip(Project& project, const std::string& track, const std::string& clipId, double lengthQ);
bool duplicateClip(Project& project, const std::string& track, const std::string& clipId);

// AUD. Slip the media under the clip.
bool slipClip(Project& project, const std::string& track, const std::string& clipId, double offsetQ);

// MID. Note add, delete, move, and grid quantize. Quantize returns how many notes moved, or -1.
bool addNote(Project& project, const std::string& track, const std::string& clipId, const Note& note);
bool deleteNote(Project& project, const std::string& track, const std::string& clipId, int index);
bool moveNote(Project& project, const std::string& track, const std::string& clipId, int index, double startQ, int pitch);
int quantizeNotes(Project& project, const std::string& track, const std::string& clipId, double gridQ);

// Warp markers, sorted by output time.
bool setWarp(Project& project, const std::string& track, const std::string& clipId, std::vector<WarpMark> marks);

// SES-04. Copies the track's first arrangement clip into a session slot.
bool captureSlot(Project& project, const std::string& track, int scene);

} // namespace ghovlz
