#pragma once

#include "model/Project.h"

#include <string>

namespace ghovlz {

// Autosave folder. A later launch can open it when the process did not exit cleanly.
class RecoveryStore {
public:
    explicit RecoveryStore(std::string directory);
    const std::string& directory() const { return directory_; }
    bool save(const Project& project) const;
    bool available() const;
    bool load(Project& project) const;
    void clear() const;

private:
    std::string directory_;
};

} // namespace ghovlz
