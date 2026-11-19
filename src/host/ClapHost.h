#pragma once

#include <string>

namespace ghovlz {

// CLAP hosting is stubbed in v0.1. The interface is the place a CLAP backend
// would plug in beside the VST 3 host. No CLAP SDK is fetched.
class ClapHost {
public:
    bool supported() const { return false; }
    std::string status() const { return "CLAP hosting is not implemented in v0.1"; }
};

} // namespace ghovlz
