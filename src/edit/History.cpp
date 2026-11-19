#include "edit/History.h"

namespace ghovlz {

bool History::undo(Project& project) {
    if (undo_.empty())
        return false;
    redo_.push_back(project);
    project = std::move(undo_.back());
    undo_.pop_back();
    return true;
}

bool History::redo(Project& project) {
    if (redo_.empty())
        return false;
    undo_.push_back(project);
    project = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

void History::clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace ghovlz
