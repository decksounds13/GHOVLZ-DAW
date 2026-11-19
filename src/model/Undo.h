#pragma once

#include "model/Project.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ghovlz {

// One command stack. Each edit stores the values it replaced and can invert them.
class UndoStack {
public:
    bool setVolume(Project& project, const std::string& track, float db);
    bool setParam(Project& project, const std::string& device, const std::string& param, float value);
    bool setSend(Project& project, const std::string& track, const std::string& target, float db);
    bool moveClip(Project& project, const std::string& track, const std::string& clip, double startQ);
    bool addDevice(Project& project, const std::string& track, const std::string& type);
    bool undo(Project& project);
    bool redo(Project& project);
    int depth() const { return static_cast<int>(undo_.size()); }
    void clear();

private:
    struct Cmd {
        enum class Kind { Volume, Param, Send, Move, Device };
        Kind kind = Kind::Volume;
        std::string a, b, c;
        float before = 0, after = 0;
        double beforeD = 0, afterD = 0;
    };
    std::vector<Cmd> undo_;
    std::vector<Cmd> redo_;
    static void apply(Project& project, const Cmd& cmd, bool inverse);
};

uint64_t projectHash(const Project& project);

} // namespace ghovlz
