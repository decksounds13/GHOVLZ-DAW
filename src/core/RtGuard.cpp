#include "core/RtGuard.h"

#include <cstdio>
#include <cstdlib>

namespace ghovlz {

std::atomic<int>& rtAudioReaders() {
    static std::atomic<int> n {0};
    return n;
}
std::atomic<int>& rtGuardHits() {
    static std::atomic<int> n {0};
    return n;
}

void rtGuardHit(const char* why) {
    rtGuardHits().fetch_add(1, std::memory_order_relaxed);
    if (rtGuardSilence > 0)
        return;
#if defined(GHOVLZ_RT_ABORT)
    std::fprintf(stderr, "real-time guard: %s\n", why ? why : "hit");
    std::abort();
#else
    (void)why;
#endif
}

} // namespace ghovlz
