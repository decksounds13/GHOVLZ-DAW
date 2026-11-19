#include "edit/Recovery.h"

#include <filesystem>

namespace ghovlz {

RecoveryStore::RecoveryStore(std::string directory) : directory_(std::move(directory)) {}

bool RecoveryStore::save(const Project& project) const {
    return project.saveToFolder(directory_);
}

bool RecoveryStore::available() const {
    return std::filesystem::exists(directory_ + "/project.json");
}

bool RecoveryStore::load(Project& project) const {
    if (!available())
        return false;
    return project.loadFromFolder(directory_);
}

void RecoveryStore::clear() const {
    std::error_code ec;
    std::filesystem::remove_all(directory_, ec);
}

} // namespace ghovlz
