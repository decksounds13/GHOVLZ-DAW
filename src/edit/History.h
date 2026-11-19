#pragma once

#include "model/Project.h"

#include <functional>
#include <vector>

namespace ghovlz {

// One command stack for every project edit. Each entry is the project as it
// was before the edit. Undo restores that copy. Redo restores the inverse.
class History {
public:
    template <typename Fn>
    bool edit(Project& project, Fn&& fn) {
        Project before = project;
        if (!fn(project))
            return false;
        undo_.push_back(std::move(before));
        redo_.clear();
        return true;
    }

    bool undo(Project& project);
    bool redo(Project& project);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    int depth() const { return static_cast<int>(undo_.size()); }
    void clear();

private:
    std::vector<Project> undo_;
    std::vector<Project> redo_;
};

} // namespace ghovlz
